#include "geo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint32_t start;
    uint32_t end;
    char country[3];
} range_t;

struct geo_db {
    range_t *ranges;
    size_t count;
};

typedef struct {
    char country[3];
    char zone[32];
} zone_entry_t;

struct zone_map {
    zone_entry_t *entries;
    size_t count;
};

int geo_parse_ipv4(const char *text, uint32_t *ip) {
    unsigned parts[4];
    char tail;
    if (sscanf(text, "%u.%u.%u.%u%c", &parts[0], &parts[1], &parts[2], &parts[3], &tail) != 4)
        return -1;
    for (int i = 0; i < 4; i++)
        if (parts[i] > 255) return -1;
    *ip = (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3];
    return 0;
}

static int cmp_range(const void *a, const void *b) {
    uint32_t x = ((const range_t *)a)->start, y = ((const range_t *)b)->start;
    return (x > y) - (x < y);
}

geo_db_t *geo_load(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;

    geo_db_t *db = calloc(1, sizeof(*db));
    size_t cap = 1024;
    db->ranges = malloc(cap * sizeof(range_t));
    if (!db->ranges) {
        free(db);
        fclose(f);
        return NULL;
    }

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char *first = strtok(line, ",");
        char *last = strtok(NULL, ",");
        char *cc = strtok(NULL, ",\r\n");
        if (!first || !last || !cc || strlen(cc) != 2) continue; /* encabezado, IPv6 o línea rota */

        range_t r;
        if (geo_parse_ipv4(first, &r.start) != 0 || geo_parse_ipv4(last, &r.end) != 0) continue;
        if (r.end < r.start) continue;
        memcpy(r.country, cc, 3);

        if (db->count == cap) {
            cap *= 2;
            range_t *grown = realloc(db->ranges, cap * sizeof(range_t));
            if (!grown) {
                geo_free(db);
                fclose(f);
                return NULL;
            }
            db->ranges = grown;
        }
        db->ranges[db->count++] = r;
    }
    fclose(f);

    qsort(db->ranges, db->count, sizeof(range_t), cmp_range);
    return db;
}

void geo_free(geo_db_t *db) {
    if (!db) return;
    free(db->ranges);
    free(db);
}

size_t geo_size(const geo_db_t *db) { return db ? db->count : 0; }

int geo_country(const geo_db_t *db, uint32_t ip, char out[3]) {
    if (!db || db->count == 0) return -1;
    size_t lo = 0, hi = db->count;
    while (lo < hi) { /* último rango cuyo inicio es <= ip */
        size_t mid = lo + (hi - lo) / 2;
        if (db->ranges[mid].start <= ip) lo = mid + 1;
        else hi = mid;
    }
    if (lo == 0) return -1;
    const range_t *r = &db->ranges[lo - 1];
    if (ip > r->end) return -1;
    memcpy(out, r->country, 3);
    return 0;
}

zone_map_t *zones_load(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;

    zone_map_t *map = calloc(1, sizeof(*map));
    size_t cap = 64;
    map->entries = malloc(cap * sizeof(zone_entry_t));
    if (!map->entries) {
        free(map);
        fclose(f);
        return NULL;
    }

    char line[128];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        char *cc = strtok(line, ",");
        char *zone = strtok(NULL, ",\r\n");
        if (!cc || !zone || strlen(cc) != 2 || strlen(zone) >= sizeof(map->entries[0].zone)) continue;

        if (map->count == cap) {
            cap *= 2;
            zone_entry_t *grown = realloc(map->entries, cap * sizeof(zone_entry_t));
            if (!grown) {
                zones_free(map);
                fclose(f);
                return NULL;
            }
            map->entries = grown;
        }
        zone_entry_t *e = &map->entries[map->count++];
        memcpy(e->country, cc, 3);
        strcpy(e->zone, zone);
    }
    fclose(f);
    return map;
}

void zones_free(zone_map_t *map) {
    if (!map) return;
    free(map->entries);
    free(map);
}

const char *zones_lookup(const zone_map_t *map, const char *country) {
    if (!map || !country) return NULL;
    for (size_t i = 0; i < map->count; i++)
        if (strcmp(map->entries[i].country, country) == 0) return map->entries[i].zone;
    return NULL;
}
