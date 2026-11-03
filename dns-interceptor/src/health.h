#ifndef HEALTH_H
#define HEALTH_H

#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>

#include "config.h"

/* Estado de salud de cada caché zonal (mismo orden que cfg->zones). */
typedef struct {
    const config_t *cfg;
    atomic_int up[CFG_MAX_ZONES];
    volatile sig_atomic_t *stop;
} health_t;

/* Marca todas las zonas como sanas (estado inicial, y estado permanente si no hay sondeo). */
void health_init(health_t *h, const config_t *cfg, volatile sig_atomic_t *stop);

/* Copia el estado actual: out[i] = 1 si la zona i está sana. */
void health_snapshot(health_t *h, int out[CFG_MAX_ZONES]);

/* Intenta una conexión TCP a ip:port (ip en orden de host). Devuelve 1 si conecta a tiempo. */
int health_probe(uint32_t ip, int port, int timeout_ms);

/* Cuerpo del hilo de sondeo: revisa cada zona cada cfg->health_interval_seconds. */
void *health_loop(void *arg);

#endif
