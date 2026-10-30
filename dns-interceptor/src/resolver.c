#include "resolver.h"

#include <stdio.h>
#include <string.h>

int resolver_choose(const config_t *cfg, const geo_db_t *geo, const zone_map_t *zones,
                    uint32_t client_ip, zone_choice_t *out) {
    strcpy(out->country, "ZZ");
    if (geo_country(geo, client_ip, out->country) != 0) strcpy(out->country, "ZZ");

    const char *zone = zones_lookup(zones, out->country);
    if (!zone || config_zone_ip(cfg, zone, &out->ip) != 0) zone = cfg->default_zone;

    snprintf(out->zone, sizeof(out->zone), "%s", zone);
    return config_zone_ip(cfg, zone, &out->ip);
}
