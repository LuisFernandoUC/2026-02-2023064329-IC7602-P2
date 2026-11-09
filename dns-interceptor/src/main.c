#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "config.h"
#include "dns.h"
#include "domains.h"
#include "geo.h"
#include "health.h"
#include "resolver.h"
#include "sync.h"

/* Consultas en curso (UDP + TCP). Pasado este tope se descartan las nuevas en vez de crear
 * hilos sin límite. */
#define MAX_INFLIGHT 256
#define UDP_REPLY_MAX 65535
#define TCP_MAX_QUERY 4096
#define TCP_MAX_REPLY 65535
#define TCP_IDLE_SECONDS 5
#define TCP_MAX_QUERIES_PER_CONN 100

typedef struct {
    config_t cfg;
    geo_db_t *geo;
    zone_map_t *zones;
    dyn_domains_t dyn;
    health_t health;
    int sock;     /* UDP */
    int tcp_sock; /* TCP, -1 si está desactivado */
    struct sockaddr_in upstream;
} server_t;

typedef struct {
    server_t *srv;
    struct sockaddr_in client;
    uint8_t data[DNS_MAX_PACKET];
    size_t len;
} job_t;

typedef struct {
    server_t *srv;
    int fd;
    uint32_t peer_ip;
} tcp_job_t;

static volatile sig_atomic_t stop_requested = 0;
static atomic_int inflight = 0;

static void on_signal(int sig) {
    (void)sig;
    stop_requested = 1;
}

static void ip_text(uint32_t ip, char out[INET_ADDRSTRLEN]) {
    snprintf(out, INET_ADDRSTRLEN, "%u.%u.%u.%u", ip >> 24, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255);
}

static void set_timeouts(int fd, int ms) {
    struct timeval tv = {.tv_sec = ms / 1000, .tv_usec = (ms % 1000) * 1000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

static int read_full(int fd, uint8_t *buf, size_t n) {
    size_t got = 0;
    while (got < n) {
        ssize_t r = recv(fd, buf + got, n - got, 0);
        if (r <= 0) return -1;
        got += (size_t)r;
    }
    return 0;
}

static int write_full(int fd, const uint8_t *buf, size_t n) {
    size_t sent = 0;
    while (sent < n) {
        ssize_t w = send(fd, buf + sent, n - sent, 0);
        if (w <= 0) return -1;
        sent += (size_t)w;
    }
    return 0;
}

static int is_intercepted(server_t *srv, const char *name) {
    return config_domain_match(&srv->cfg, name) || dyn_domains_match(&srv->dyn, name);
}

/*
 * Lógica común a UDP y TCP. Con una consulta válida:
 *  - dominio no interceptado: *forward = 1 y devuelve 0 (hay que reenviarla al DNS externo);
 *  - dominio interceptado: devuelve la respuesta ya armada en out.
 * Con un paquete inválido devuelve FORMERR/NOTIMP (o 0 si no corresponde contestar).
 */
static size_t answer_local(server_t *srv, const uint8_t *data, size_t len, uint32_t peer_ip,
                           uint8_t *out, size_t outcap, int *forward) {
    dns_query_t q;
    *forward = 0;

    if (dns_parse_query(data, len, &q) != 0) return dns_build_error(data, len, out, outcap);

    if (!is_intercepted(srv, q.name)) {
        *forward = 1;
        return 0;
    }

    /* Dominio interceptado: solo se responden registros A; para otros tipos, NODATA. */
    if (q.qtype != DNS_TYPE_A || q.qclass != DNS_CLASS_IN)
        return dns_build_empty_response(data, &q, DNS_RCODE_NOERROR, out, outcap);

    /* Con EDNS Client Subnet se usa la subred indicada; si no, la IP de origen del paquete. */
    uint32_t client = q.has_ecs ? q.ecs_ip : peer_ip;
    zone_choice_t pick;
    char cip[INET_ADDRSTRLEN], zip[INET_ADDRSTRLEN] = "-";
    int up[CFG_MAX_ZONES];
    size_t n;

    health_snapshot(&srv->health, up);
    ip_text(client, cip);
    if (resolver_choose(&srv->cfg, srv->geo, srv->zones, client, up, &pick) == 0) {
        ip_text(pick.ip, zip);
        n = dns_build_a_response(data, &q, pick.ip, srv->cfg.ttl, out, outcap);
    } else {
        pick.failover = 0;
        n = dns_build_empty_response(data, &q, DNS_RCODE_SERVFAIL, out, outcap);
    }
    printf("consulta=%s cliente=%s%s pais=%s zona=%s respuesta=%s%s\n", q.name, cip,
           q.has_ecs ? "(ecs)" : "", pick.country, pick.zone, zip,
           pick.failover ? " desvio=si" : "");
    fflush(stdout);
    return n;
}

/* ---------- UDP ---------- */

/* Reenvía la consulta tal cual al DNS externo y devuelve su respuesta al cliente. */
static void forward_udp(job_t *job) {
    server_t *srv = job->srv;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return;
    set_timeouts(fd, srv->cfg.upstream_timeout_ms);

    /* Buffer grande: un recv con buffer corto recorta el datagrama y el cliente recibiría basura. */
    uint8_t *reply = malloc(UDP_REPLY_MAX);
    if (reply && sendto(fd, job->data, job->len, 0, (struct sockaddr *)&srv->upstream, sizeof(srv->upstream)) >= 0) {
        ssize_t n = recv(fd, reply, UDP_REPLY_MAX, 0);
        if (n > 0)
            sendto(srv->sock, reply, (size_t)n, 0, (struct sockaddr *)&job->client, sizeof(job->client));
        else
            fprintf(stderr, "upstream sin respuesta (%s)\n", strerror(errno));
    }
    free(reply);
    close(fd);
}

static void *handle_udp(void *arg) {
    job_t *job = arg;
    server_t *srv = job->srv;
    uint8_t out[DNS_MAX_PACKET];
    int forward;

    size_t n = answer_local(srv, job->data, job->len, ntohl(job->client.sin_addr.s_addr), out,
                            sizeof(out), &forward);
    if (forward)
        forward_udp(job);
    else if (n > 0)
        sendto(srv->sock, out, n, 0, (struct sockaddr *)&job->client, sizeof(job->client));

    free(job);
    atomic_fetch_sub(&inflight, 1);
    return NULL;
}

/* ---------- TCP (RFC 7766: cada mensaje lleva 2 bytes con su largo) ---------- */

/* Reenvía la consulta al DNS externo por TCP. Devuelve el largo de la respuesta o 0 si falla. */
static size_t forward_tcp(server_t *srv, const uint8_t *data, size_t len, uint8_t *reply, size_t cap) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return 0;
    set_timeouts(fd, srv->cfg.upstream_timeout_ms);

    size_t rlen = 0;
    uint8_t hdr[2] = {(uint8_t)(len >> 8), (uint8_t)len};
    if (connect(fd, (struct sockaddr *)&srv->upstream, sizeof(srv->upstream)) == 0 &&
        write_full(fd, hdr, 2) == 0 && write_full(fd, data, len) == 0 && read_full(fd, hdr, 2) == 0) {
        size_t want = ((size_t)hdr[0] << 8) | hdr[1];
        if (want > 0 && want <= cap && read_full(fd, reply, want) == 0) rlen = want;
    }
    if (rlen == 0) fprintf(stderr, "upstream TCP sin respuesta (%s)\n", strerror(errno));
    close(fd);
    return rlen;
}

static void *handle_tcp(void *arg) {
    tcp_job_t *job = arg;
    server_t *srv = job->srv;
    int fd = job->fd;
    uint8_t query[TCP_MAX_QUERY], out[DNS_MAX_PACKET];
    uint8_t *reply = NULL; /* solo se reserva si hay que reenviar al DNS externo */

    set_timeouts(fd, TCP_IDLE_SECONDS * 1000);
    for (int i = 0; i < TCP_MAX_QUERIES_PER_CONN && !stop_requested; i++) {
        uint8_t hdr[2];
        if (read_full(fd, hdr, 2) != 0) break;
        size_t qlen = ((size_t)hdr[0] << 8) | hdr[1];
        if (qlen == 0 || qlen > sizeof(query) || read_full(fd, query, qlen) != 0) break;

        int forward;
        size_t n = answer_local(srv, query, qlen, job->peer_ip, out, sizeof(out), &forward);
        const uint8_t *resp = out;
        if (forward) {
            if (!reply && !(reply = malloc(TCP_MAX_REPLY))) break;
            n = forward_tcp(srv, query, qlen, reply, TCP_MAX_REPLY);
            resp = reply;
        }
        if (n == 0) continue; /* nada que contestar (paquete ignorado o upstream caído) */

        hdr[0] = (uint8_t)(n >> 8);
        hdr[1] = (uint8_t)n;
        if (write_full(fd, hdr, 2) != 0 || write_full(fd, resp, n) != 0) break;
    }
    free(reply);
    close(fd);
    free(job);
    atomic_fetch_sub(&inflight, 1);
    return NULL;
}

static void *tcp_accept_loop(void *arg) {
    server_t *srv = arg;
    struct pollfd p = {.fd = srv->tcp_sock, .events = POLLIN};

    while (!stop_requested) {
        if (poll(&p, 1, 500) <= 0) continue; /* despierta cada 500 ms para revisar SIGTERM */
        struct sockaddr_in peer;
        socklen_t plen = sizeof(peer);
        int fd = accept(srv->tcp_sock, (struct sockaddr *)&peer, &plen);
        if (fd < 0) continue;

        tcp_job_t *job = malloc(sizeof(*job));
        if (!job || atomic_fetch_add(&inflight, 1) >= MAX_INFLIGHT) {
            if (job) atomic_fetch_sub(&inflight, 1);
            free(job);
            close(fd);
            continue;
        }
        job->srv = srv;
        job->fd = fd;
        job->peer_ip = ntohl(peer.sin_addr.s_addr);

        pthread_t t;
        if (pthread_create(&t, NULL, handle_tcp, job) != 0) {
            atomic_fetch_sub(&inflight, 1);
            close(fd);
            free(job);
            continue;
        }
        pthread_detach(t);
    }
    return NULL;
}

/* ---------- arranque ---------- */

/* Modo de diagnóstico: imprime el país y la zona de una IP y termina. */
static int lookup_mode(server_t *srv, const char *text) {
    uint32_t ip;
    zone_choice_t pick;
    if (geo_parse_ipv4(text, &ip) != 0) {
        fprintf(stderr, "IP inválida: %s\n", text);
        return 1;
    }
    char zip[INET_ADDRSTRLEN] = "-";
    if (resolver_choose(&srv->cfg, srv->geo, srv->zones, ip, NULL, &pick) == 0) ip_text(pick.ip, zip);
    printf("ip=%s pais=%s zona=%s cache=%s\n", text, pick.country, pick.zone, zip);
    return 0;
}

static int open_tcp_listener(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    if (fd < 0) return -1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = htons((uint16_t)port),
                               .sin_addr.s_addr = htonl(INADDR_ANY)};
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0 || listen(fd, 64) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

int main(int argc, char **argv) {
    static server_t srv;
    char err[256];

    setvbuf(stdout, NULL, _IOLBF, 0);

    if (config_from_env(&srv.cfg, err, sizeof(err)) != 0) {
        fprintf(stderr, "configuración inválida: %s\n", err);
        return 1;
    }
    srv.geo = geo_load(srv.cfg.country_db);
    if (!srv.geo || geo_size(srv.geo) == 0) {
        fprintf(stderr, "no se pudo cargar la base IP to Country: %s\n", srv.cfg.country_db);
        return 1;
    }
    srv.zones = zones_load(srv.cfg.zones_file);
    if (!srv.zones) {
        fprintf(stderr, "no se pudo cargar la tabla país->zona: %s\n", srv.cfg.zones_file);
        return 1;
    }

    if (argc == 3 && strcmp(argv[1], "--lookup") == 0) return lookup_mode(&srv, argv[2]);

    /* El DNS externo puede venir como nombre de host o como IP. */
    struct addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_DGRAM}, *res = NULL;
    char port[8];
    snprintf(port, sizeof(port), "%d", srv.cfg.upstream_port);
    if (getaddrinfo(srv.cfg.upstream_host, port, &hints, &res) != 0 || !res) {
        fprintf(stderr, "no se pudo resolver DI_UPSTREAM_DNS: %s\n", srv.cfg.upstream_host);
        return 1;
    }
    memcpy(&srv.upstream, res->ai_addr, sizeof(srv.upstream));
    freeaddrinfo(res);

    srv.sock = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = htons((uint16_t)srv.cfg.port),
                               .sin_addr.s_addr = htonl(INADDR_ANY)};
    if (srv.sock < 0 || bind(srv.sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        fprintf(stderr, "no se pudo abrir el puerto UDP %d: %s\n", srv.cfg.port, strerror(errno));
        return 1;
    }
    set_timeouts(srv.sock, 500); /* recvfrom despierta cada 500 ms para revisar SIGTERM */

    srv.tcp_sock = -1;
    if (srv.cfg.tcp_enabled) {
        srv.tcp_sock = open_tcp_listener(srv.cfg.port);
        if (srv.tcp_sock < 0) {
            fprintf(stderr, "no se pudo abrir el puerto TCP %d: %s\n", srv.cfg.port, strerror(errno));
            return 1;
        }
    }

    /* Como PID 1 en un contenedor, SIGTERM solo termina el proceso si hay un manejador. */
    struct sigaction sa = {.sa_handler = on_signal};
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    signal(SIGPIPE, SIG_IGN); /* un cliente TCP que se va no debe matar el proceso */

    dyn_domains_init(&srv.dyn);
    health_init(&srv.health, &srv.cfg, &stop_requested);

    pthread_t t;
    if (srv.tcp_sock >= 0 && pthread_create(&t, NULL, tcp_accept_loop, &srv) == 0) pthread_detach(t);

    static sync_ctx_t sync_ctx;
    sync_ctx = (sync_ctx_t){.cfg = &srv.cfg, .dyn = &srv.dyn, .stop = &stop_requested};
    if (srv.cfg.domains_url[0] && pthread_create(&t, NULL, domains_sync_loop, &sync_ctx) == 0)
        pthread_detach(t);

    if (srv.cfg.health_interval_seconds > 0 && pthread_create(&t, NULL, health_loop, &srv.health) == 0)
        pthread_detach(t);

    printf("dns-interceptor escuchando en UDP/%d%s: %zu rangos IP, %zu zonas, %zu dominios fijos%s%s\n",
           srv.cfg.port, srv.tcp_sock >= 0 ? " y TCP" : "", geo_size(srv.geo), srv.cfg.n_zones,
           srv.cfg.n_domains, srv.cfg.domains_url[0] ? ", dominios del API activados" : "",
           srv.cfg.health_interval_seconds > 0 ? ", respaldo de zonas activado" : "");

    while (!stop_requested) {
        job_t *job = malloc(sizeof(*job));
        if (!job) continue;
        job->srv = &srv;
        socklen_t alen = sizeof(job->client);
        ssize_t n = recvfrom(srv.sock, job->data, sizeof(job->data), 0, (struct sockaddr *)&job->client, &alen);
        if (n <= 0 || atomic_fetch_add(&inflight, 1) >= MAX_INFLIGHT) {
            if (n > 0) atomic_fetch_sub(&inflight, 1);
            free(job);
            continue;
        }
        job->len = (size_t)n;

        if (pthread_create(&t, NULL, handle_udp, job) != 0) {
            atomic_fetch_sub(&inflight, 1);
            free(job);
            continue;
        }
        pthread_detach(t);
    }

    close(srv.sock);
    if (srv.tcp_sock >= 0) close(srv.tcp_sock);
    geo_free(srv.geo);
    zones_free(srv.zones);
    return 0;
}
