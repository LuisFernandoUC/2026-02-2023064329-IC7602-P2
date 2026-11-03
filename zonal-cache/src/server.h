/*
 * server.h - Servidor TCP (HTTP) y TLS (HTTPS, SSL termination) con un hilo por conexión.
 */
#ifndef ZC_SERVER_H
#define ZC_SERVER_H

#include <signal.h>

#include "config_store.h"
#include "proxy.h"
#include "settings.h"

typedef struct {
    const settings_t *settings;
    const proxy_t *proxy;
    config_store_t *configs;
} server_ctx_t;

/* Escucha hasta que *stop sea distinto de 0. Devuelve 0 al terminar o -1 si no pudo iniciar. */
int server_run(server_ctx_t *ctx, volatile sig_atomic_t *stop);

#endif
