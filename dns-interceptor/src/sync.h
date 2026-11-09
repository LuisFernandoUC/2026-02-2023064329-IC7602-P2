#ifndef SYNC_H
#define SYNC_H

#include <signal.h>

#include "config.h"
#include "domains.h"

typedef struct {
    const config_t *cfg;
    dyn_domains_t *dyn;
    volatile sig_atomic_t *stop;
} sync_ctx_t;

/* Cuerpo del hilo que consulta cfg->domains_url cada cfg->domains_refresh_seconds y actualiza
 * la lista de dominios dinámicos. Si el API falla se conserva la última lista buena. */
void *domains_sync_loop(void *arg);

#endif
