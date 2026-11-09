#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../src/health.h"
#include "test.h"

/* Abre un servidor TCP en 127.0.0.1 con puerto libre y devuelve su descriptor. */
static int listen_local(int *port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    socklen_t len = sizeof(a);
    if (fd < 0 || bind(fd, (struct sockaddr *)&a, sizeof(a)) != 0 || listen(fd, 4) != 0) return -1;
    getsockname(fd, (struct sockaddr *)&a, &len);
    *port = ntohs(a.sin_port);
    return fd;
}

void run_health_tests(void) {
    int port = 0;
    int fd = listen_local(&port);
    CHECK(fd >= 0);
    if (fd < 0) return;
    uint32_t loopback = 0x7F000001u;

    CHECK(health_probe(loopback, port, 1000) == 1); /* hay alguien escuchando */
    close(fd);
    CHECK(health_probe(loopback, port, 1000) == 0); /* ya no: conexión rechazada */

    /* Estado inicial: todas las zonas sanas. */
    config_t cfg = {0};
    volatile sig_atomic_t stop = 0;
    health_t h;
    int snap[CFG_MAX_ZONES];
    health_init(&h, &cfg, &stop);
    health_snapshot(&h, snap);
    CHECK(snap[0] == 1 && snap[CFG_MAX_ZONES - 1] == 1);
}
