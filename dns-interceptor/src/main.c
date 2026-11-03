#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "config.h"
#include "dns.h"
#include "geo.h"
#include "resolver.h"

typedef struct {
    config_t cfg;
    geo_db_t *geo;
    zone_map_t *zones;
    int sock;
    struct sockaddr_in upstream;
} server_t;

typedef struct {
    server_t *srv;
    struct sockaddr_in client;
    uint8_t data[DNS_MAX_PACKET];
    size_t len;
} job_t;

static volatile sig_atomic_t stop_requested = 0;

static void on_signal(int sig) {
    (void)sig;
    stop_requested = 1;
}

static void ip_text(uint32_t ip, char out[INET_ADDRSTRLEN]) {
    snprintf(out, INET_ADDRSTRLEN, "%u.%u.%u.%u", ip >> 24, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255);
}

/* Reenvía la consulta tal cual al DNS externo y devuelve su respuesta al cliente. */
static void forward_upstream(job_t *job) {
    server_t *srv = job->srv;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return;

    struct timeval tv = {.tv_sec = srv->cfg.upstream_timeout_ms / 1000,
                         .tv_usec = (srv->cfg.upstream_timeout_ms % 1000) * 1000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    uint8_t reply[DNS_MAX_PACKET];
    if (sendto(fd, job->data, job->len, 0, (struct sockaddr *)&srv->upstream, sizeof(srv->upstream)) >= 0) {
        ssize_t n = recv(fd, reply, sizeof(reply), 0);
        if (n > 0)
            sendto(srv->sock, reply, (size_t)n, 0, (struct sockaddr *)&job->client, sizeof(job->client));
        else
            fprintf(stderr, "upstream sin respuesta (%s)\n", strerror(errno));
    }
    close(fd);
}

static void *handle_query(void *arg) {
    job_t *job = arg;
    server_t *srv = job->srv;
    dns_query_t q;
    uint8_t out[DNS_MAX_PACKET];
    size_t n = 0;

    if (dns_parse_query(job->data, job->len, &q) != 0) goto done; /* paquete inválido: se ignora */

    if (!config_domain_match(&srv->cfg, q.name)) {
        forward_upstream(job);
        goto done;
    }

    /* Dominio interceptado: solo se responden registros A; para otros tipos, NODATA. */
    if (q.qtype != DNS_TYPE_A || q.qclass != DNS_CLASS_IN) {
        n = dns_build_empty_response(job->data, &q, DNS_RCODE_NOERROR, out, sizeof(out));
    } else {
        /* Con EDNS Client Subnet se usa la subred indicada; si no, la IP de origen del paquete. */
        uint32_t client = q.has_ecs ? q.ecs_ip : ntohl(job->client.sin_addr.s_addr);
        zone_choice_t pick;
        char cip[INET_ADDRSTRLEN], zip[INET_ADDRSTRLEN] = "-";

        ip_text(client, cip);
        if (resolver_choose(&srv->cfg, srv->geo, srv->zones, client, &pick) == 0) {
            ip_text(pick.ip, zip);
            n = dns_build_a_response(job->data, &q, pick.ip, srv->cfg.ttl, out, sizeof(out));
        } else {
            n = dns_build_empty_response(job->data, &q, DNS_RCODE_SERVFAIL, out, sizeof(out));
        }
        printf("consulta=%s cliente=%s%s pais=%s zona=%s respuesta=%s\n", q.name, cip,
               q.has_ecs ? "(ecs)" : "", pick.country, pick.zone, zip);
        fflush(stdout);
    }
    if (n > 0)
        sendto(srv->sock, out, n, 0, (struct sockaddr *)&job->client, sizeof(job->client));

done:
    free(job);
    return NULL;
}

/* Modo de diagnóstico: imprime el país y la zona de una IP y termina. */
static int lookup_mode(server_t *srv, const char *text) {
    uint32_t ip;
    zone_choice_t pick;
    if (geo_parse_ipv4(text, &ip) != 0) {
        fprintf(stderr, "IP inválida: %s\n", text);
        return 1;
    }
    char zip[INET_ADDRSTRLEN] = "-";
    if (resolver_choose(&srv->cfg, srv->geo, srv->zones, ip, &pick) == 0) ip_text(pick.ip, zip);
    printf("ip=%s pais=%s zona=%s cache=%s\n", text, pick.country, pick.zone, zip);
    return 0;
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

    /* Como PID 1 en un contenedor, SIGTERM solo termina el proceso si hay un manejador. */
    struct sigaction sa = {.sa_handler = on_signal};
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    printf("dns-interceptor escuchando en UDP/%d: %zu rangos IP, %zu zonas, %zu dominios interceptados\n",
           srv.cfg.port, geo_size(srv.geo), srv.cfg.n_zones, srv.cfg.n_domains);

    while (!stop_requested) {
        job_t *job = malloc(sizeof(*job));
        if (!job) continue;
        job->srv = &srv;
        socklen_t alen = sizeof(job->client);
        ssize_t n = recvfrom(srv.sock, job->data, sizeof(job->data), 0, (struct sockaddr *)&job->client, &alen);
        if (n <= 0) {
            free(job);
            continue;
        }
        job->len = (size_t)n;

        pthread_t t;
        if (pthread_create(&t, NULL, handle_query, job) != 0) {
            free(job);
            continue;
        }
        pthread_detach(t);
    }

    close(srv.sock);
    geo_free(srv.geo);
    zones_free(srv.zones);
    return 0;
}
