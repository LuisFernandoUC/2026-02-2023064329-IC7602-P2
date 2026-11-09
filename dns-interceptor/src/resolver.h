#ifndef RESOLVER_H
#define RESOLVER_H

#include <stdint.h>

#include "config.h"
#include "geo.h"

typedef struct {
    char country[3]; /* "ZZ" si no se pudo determinar */
    char zone[CFG_NAME_LEN];
    uint32_t ip;  /* IP de la caché zonal elegida, en orden de host */
    int failover; /* 1 si se desvió a otra zona porque la que correspondía no responde */
} zone_choice_t;

/* Elige la caché zonal para un cliente: país (IP to Country) -> zona -> IP.
 * Si el país es desconocido o no tiene zona, usa la zona por defecto.
 *
 * zone_up (mismo orden que cfg->zones, 1 = sana) permite el respaldo: si la zona del país no
 * responde se usa la zona por defecto y, si tampoco, la primera zona sana. Con NULL todas se
 * consideran sanas. Si ninguna responde se mantiene la zona del país (mejor eso que un error).
 *
 * Devuelve 0 si hay una IP para responder y -1 si no hay ninguna. */
int resolver_choose(const config_t *cfg, const geo_db_t *geo, const zone_map_t *zones,
                    uint32_t client_ip, const int *zone_up, zone_choice_t *out);

#endif
