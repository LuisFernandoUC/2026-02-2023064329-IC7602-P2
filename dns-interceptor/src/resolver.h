#ifndef RESOLVER_H
#define RESOLVER_H

#include <stdint.h>

#include "config.h"
#include "geo.h"

typedef struct {
    char country[3]; /* "ZZ" si no se pudo determinar */
    char zone[CFG_NAME_LEN];
    uint32_t ip; /* IP de la caché zonal elegida, en orden de host */
} zone_choice_t;

/* Elige la caché zonal para un cliente: país (IP to Country) -> zona -> IP.
 * Si el país es desconocido o no tiene zona, usa la zona por defecto.
 * Devuelve 0 si hay una IP para responder y -1 si no hay ninguna. */
int resolver_choose(const config_t *cfg, const geo_db_t *geo, const zone_map_t *zones,
                    uint32_t client_ip, zone_choice_t *out);

#endif
