#include "resolver.h"

#include <stdio.h>
#include <string.h>

static int zone_index(const config_t *cfg, const char *name) {
    for (size_t i = 0; i < cfg->n_zones; i++)
        if (strcmp(cfg->zones[i].name, name) == 0) return (int)i;
    return -1;
}

int resolver_choose(const config_t *cfg, const geo_db_t *geo, const zone_map_t *zones,
                    uint32_t client_ip, const int *zone_up, zone_choice_t *out) {
    strcpy(out->country, "ZZ");
    if (geo_country(geo, client_ip, out->country) != 0) strcpy(out->country, "ZZ");
    out->failover = 0;

    int default_idx = zone_index(cfg, cfg->default_zone);
    const char *zone = zones_lookup(zones, out->country);
    int idx = zone ? zone_index(cfg, zone) : -1;
    if (idx < 0) idx = default_idx;
    if (idx < 0) return -1;

    if (zone_up && !zone_up[idx]) {
        int backup = -1;
        if (default_idx >= 0 && zone_up[default_idx]) backup = default_idx;
        for (size_t i = 0; backup < 0 && i < cfg->n_zones; i++)
            if (zone_up[i]) backup = (int)i;
        if (backup >= 0) {
            idx = backup;
            out->failover = 1;
        }
    }

    snprintf(out->zone, sizeof(out->zone), "%s", cfg->zones[idx].name);
    out->ip = cfg->zones[idx].ip;
    return 0;
}
