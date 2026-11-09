#include "health.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

void health_init(health_t *h, const config_t *cfg, volatile sig_atomic_t *stop) {
    h->cfg = cfg;
    h->stop = stop;
    for (int i = 0; i < CFG_MAX_ZONES; i++) atomic_init(&h->up[i], 1);
}

void health_snapshot(health_t *h, int out[CFG_MAX_ZONES]) {
    for (int i = 0; i < CFG_MAX_ZONES; i++) out[i] = atomic_load(&h->up[i]);
}

int health_probe(uint32_t ip, int port, int timeout_ms) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return 0;
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);

    struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = htons((uint16_t)port),
                               .sin_addr.s_addr = htonl(ip)};
    int ok = 0;
    int rc = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    if (rc == 0) {
        ok = 1;
    } else if (errno == EINPROGRESS) {
        struct pollfd p = {.fd = fd, .events = POLLOUT};
        if (poll(&p, 1, timeout_ms) == 1) {
            int err = 0;
            socklen_t len = sizeof(err);
            ok = getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err == 0;
        }
    }
    close(fd);
    return ok;
}

static void ip_text(uint32_t ip, char out[INET_ADDRSTRLEN]) {
    snprintf(out, INET_ADDRSTRLEN, "%u.%u.%u.%u", ip >> 24, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255);
}

/* Duerme en tramos cortos para reaccionar rápido a SIGTERM. */
static void sleep_seconds(int seconds, volatile sig_atomic_t *stop) {
    struct timespec ts = {.tv_sec = 0, .tv_nsec = 200 * 1000 * 1000};
    for (int i = 0; i < seconds * 5 && !*stop; i++) nanosleep(&ts, NULL);
}

void *health_loop(void *arg) {
    health_t *h = arg;
    const config_t *cfg = h->cfg;

    while (!*h->stop) {
        for (size_t i = 0; i < cfg->n_zones && !*h->stop; i++) {
            int now = health_probe(cfg->zones[i].ip, cfg->health_port, cfg->health_timeout_ms);
            int before = atomic_exchange(&h->up[i], now);
            if (now != before) {
                char ip[INET_ADDRSTRLEN];
                ip_text(cfg->zones[i].ip, ip);
                printf("zona=%s cache=%s:%d %s\n", cfg->zones[i].name, ip, cfg->health_port,
                       now ? "recuperada" : "no responde, se desvía a otra zona");
                fflush(stdout);
            }
        }
        sleep_seconds(cfg->health_interval_seconds, h->stop);
    }
    return NULL;
}
