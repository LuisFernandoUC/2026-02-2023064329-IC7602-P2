/*
 * server.c - Sockets TCP, aceptación de conexiones, terminación TLS con OpenSSL y
 * lectura/escritura de una petición HTTP por conexión.
 */
#include "server.h"
#include "log.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <poll.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define MAX_HEAD_BYTES (64 * 1024)
#define IO_TIMEOUT_SECONDS 30

typedef struct {
    int fd;
    SSL *ssl;
    int is_tls;
    char ip[INET6_ADDRSTRLEN];
    server_ctx_t *ctx;
} connection_t;

static long conn_read(connection_t *c, void *buf, size_t len)
{
    if (c->ssl) {
        int n = SSL_read(c->ssl, buf, (int)len);
        return n > 0 ? n : (SSL_get_error(c->ssl, n) == SSL_ERROR_ZERO_RETURN ? 0 : -1);
    }
    ssize_t n;
    do {
        n = recv(c->fd, buf, len, 0);
    } while (n < 0 && errno == EINTR);
    return (long)n;
}

static int conn_write_all(connection_t *c, const char *buf, size_t len)
{
    size_t sent = 0;
    while (sent < len) {
        long n;
        if (c->ssl) {
            n = SSL_write(c->ssl, buf + sent, (int)(len - sent));
        } else {
            n = send(c->fd, buf + sent, len - sent, MSG_NOSIGNAL);
            if (n < 0 && errno == EINTR)
                continue;
        }
        if (n <= 0)
            return -1;
        sent += (size_t)n;
    }
    return 0;
}

static void send_response(connection_t *c, http_response_t *resp)
{
    size_t len = 0;
    char *raw = http_response_serialize(resp, &len);
    if (raw) {
        conn_write_all(c, raw, len);
        free(raw);
    }
}

static void send_error(connection_t *c, int status, const char *text)
{
    http_response_t resp;
    http_response_init(&resp, status);
    http_response_text(&resp, status, text);
    send_response(c, &resp);
    http_response_free(&resp);
}

/*
 * Lee una petición completa (cabecera + cuerpo según Content-Length).
 * Devuelve 0 si tuvo éxito; si no, ya respondió el error (o el cliente se fue).
 */
static int read_request(connection_t *c, http_request_t *req)
{
    size_t max_body = c->ctx->settings->max_body_bytes;
    char *buf = malloc(MAX_HEAD_BYTES);
    if (!buf)
        return -1;
    size_t len = 0;
    long head_len = 0;

    while (head_len == 0) {
        if (len == MAX_HEAD_BYTES) {
            send_error(c, 400, "Encabezados demasiado grandes\n");
            free(buf);
            return -1;
        }
        long n = conn_read(c, buf + len, MAX_HEAD_BYTES - len);
        if (n <= 0) {
            free(buf);
            return -1;
        }
        len += (size_t)n;
        head_len = http_parse_request_head(buf, len, req);
    }
    if (head_len < 0) {
        send_error(c, 400, "Petición HTTP mal formada\n");
        free(buf);
        return -1;
    }

    const char *te = http_request_header(req, "Transfer-Encoding");
    long content_length = http_request_content_length(req);
    if (te && strcmp(te, "identity") != 0) {
        send_error(c, 411, "Se requiere Content-Length (chunked no soportado)\n");
        goto fail;
    }
    if (content_length < 0) {
        send_error(c, 400, "Content-Length inválido\n");
        goto fail;
    }
    if ((size_t)content_length > max_body) {
        send_error(c, 413, "Cuerpo demasiado grande\n");
        goto fail;
    }

    if (content_length > 0) {
        unsigned char *body = malloc((size_t)content_length);
        if (!body)
            goto fail;
        size_t have = len - (size_t)head_len;
        if (have > (size_t)content_length)
            have = (size_t)content_length;
        memcpy(body, buf + head_len, have);
        while (have < (size_t)content_length) {
            long n = conn_read(c, body + have, (size_t)content_length - have);
            if (n <= 0) {
                free(body);
                goto fail;
            }
            have += (size_t)n;
        }
        int rc = http_request_set_body(req, body, have);
        free(body);
        if (rc != 0)
            goto fail;
    }
    free(buf);
    return 0;

fail:
    http_request_free(req);
    free(buf);
    return -1;
}

static void *handle_connection(void *arg)
{
    connection_t *c = arg;
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);

    if (c->is_tls) {
        if (SSL_accept(c->ssl) <= 0) {
            LOG_D("handshake TLS fallido desde %s", c->ip);
            goto done;
        }
    }

    http_request_t req;
    if (read_request(c, &req) != 0)
        goto done;

    config_snapshot_t *snap = config_store_acquire(c->ctx->configs);
    request_ctx_t rc = {.client_ip = c->ip, .is_tls = c->is_tls, .now = time(NULL)};
    http_response_t resp;
    proxy_handle(c->ctx->proxy, config_snapshot_get(snap), &req, &rc, &resp);
    config_store_release(c->ctx->configs, snap);
    send_response(c, &resp);

    struct timespec end;
    clock_gettime(CLOCK_MONOTONIC, &end);
    long ms = (end.tv_sec - start.tv_sec) * 1000 + (end.tv_nsec - start.tv_nsec) / 1000000;
    const char *host = http_request_header(&req, "Host");
    const char *xcache = http_response_header(&resp, "X-Cache");
    LOG_I("%s %s %s%s %d %s %ldms%s", c->ip, req.method, host ? host : "-", req.target,
          resp.status, xcache ? xcache : "-", ms, c->is_tls ? " tls" : "");

    http_response_free(&resp);
    http_request_free(&req);

done:
    if (c->ssl) {
        SSL_shutdown(c->ssl);
        SSL_free(c->ssl);
    }
    close(c->fd);
    free(c);
    return NULL;
}

static int listen_on(int port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0 || listen(fd, 128) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static SSL_CTX *create_tls_context(const settings_t *s)
{
    SSL_CTX *tls = SSL_CTX_new(TLS_server_method());
    if (!tls)
        return NULL;
    SSL_CTX_set_min_proto_version(tls, TLS1_2_VERSION);
    if (SSL_CTX_use_certificate_chain_file(tls, s->tls_cert_file) != 1 ||
        SSL_CTX_use_PrivateKey_file(tls, s->tls_key_file, SSL_FILETYPE_PEM) != 1 ||
        SSL_CTX_check_private_key(tls) != 1) {
        char buf[256];
        ERR_error_string_n(ERR_get_error(), buf, sizeof(buf));
        LOG_E("no se pudo cargar el certificado TLS: %s", buf);
        SSL_CTX_free(tls);
        return NULL;
    }
    return tls;
}

static void accept_one(server_ctx_t *ctx, int listen_fd, SSL_CTX *tls)
{
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    int fd = accept(listen_fd, (struct sockaddr *)&addr, &addr_len);
    if (fd < 0)
        return;

    struct timeval tv = {.tv_sec = IO_TIMEOUT_SECONDS};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    connection_t *c = calloc(1, sizeof(*c));
    if (!c) {
        close(fd);
        return;
    }
    c->fd = fd;
    c->ctx = ctx;
    c->is_tls = tls != NULL;
    inet_ntop(AF_INET, &addr.sin_addr, c->ip, sizeof(c->ip));
    if (tls) {
        c->ssl = SSL_new(tls);
        if (!c->ssl) {
            close(fd);
            free(c);
            return;
        }
        SSL_set_fd(c->ssl, fd);
    }

    pthread_t th;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&th, &attr, handle_connection, c) != 0) {
        if (c->ssl)
            SSL_free(c->ssl);
        close(fd);
        free(c);
    }
    pthread_attr_destroy(&attr);
}

int server_run(server_ctx_t *ctx, volatile sig_atomic_t *stop)
{
    const settings_t *s = ctx->settings;
    SSL_CTX *tls = NULL;
    int http_fd = listen_on(s->http_port);
    if (http_fd < 0) {
        LOG_E("no se pudo escuchar en el puerto HTTP %d: %s", s->http_port, strerror(errno));
        return -1;
    }
    LOG_I("escuchando HTTP en el puerto %d", s->http_port);

    int https_fd = -1;
    if (s->https_port) {
        tls = create_tls_context(s);
        https_fd = tls ? listen_on(s->https_port) : -1;
        if (https_fd < 0) {
            LOG_E("no se pudo iniciar HTTPS en el puerto %d", s->https_port);
            SSL_CTX_free(tls);
            close(http_fd);
            return -1;
        }
        LOG_I("escuchando HTTPS (SSL termination) en el puerto %d", s->https_port);
    }

    struct pollfd fds[2] = {{.fd = http_fd, .events = POLLIN}, {.fd = https_fd, .events = POLLIN}};
    nfds_t nfds = https_fd >= 0 ? 2 : 1;
    while (!*stop) {
        int ready = poll(fds, nfds, 1000);
        if (ready <= 0)
            continue;
        if (fds[0].revents & POLLIN)
            accept_one(ctx, http_fd, NULL);
        if (nfds == 2 && (fds[1].revents & POLLIN))
            accept_one(ctx, https_fd, tls);
    }

    close(http_fd);
    if (https_fd >= 0)
        close(https_fd);
    SSL_CTX_free(tls);
    return 0;
}
