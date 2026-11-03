#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "geo.h"

static const char *env_or(const char *name, const char *fallback) {
    const char *v = getenv(name);
    return (v && *v) ? v : fallback;
}

static void to_lower(char *s) {
    for (; *s; s++)
        if (*s >= 'A' && *s <= 'Z') *s = (char)(*s + 32);
}

static int parse_domains(config_t *cfg, const char *text, char *err, size_t errlen) {
    char *copy = strdup(text);
    char *save = NULL;
    for (char *tok = strtok_r(copy, ", ", &save); tok; tok = strtok_r(NULL, ", ", &save)) {
        if (cfg->n_domains == CFG_MAX_DOMAINS || strlen(tok) >= CFG_NAME_LEN) {
            snprintf(err, errlen, "DI_DOMAINS: demasiados dominios o nombre muy largo ('%s')", tok);
            free(copy);
            return -1;
        }
        strcpy(cfg->domains[cfg->n_domains], tok);
        to_lower(cfg->domains[cfg->n_domains++]);
    }
    free(copy);
    return 0;
}

/* DI_ZONE_IPS = "america=10.0.0.1,europa=10.0.0.2" */
static int parse_zone_ips(config_t *cfg, const char *text, char *err, size_t errlen) {
    char *copy = strdup(text);
    char *save = NULL;
    for (char *tok = strtok_r(copy, ", ", &save); tok; tok = strtok_r(NULL, ", ", &save)) {
        char *eq = strchr(tok, '=');
        zone_ip_t z;
        if (!eq || eq == tok || (size_t)(eq - tok) >= CFG_NAME_LEN ||
            geo_parse_ipv4(eq + 1, &z.ip) != 0 || cfg->n_zones == CFG_MAX_ZONES) {
            snprintf(err, errlen, "DI_ZONE_IPS: entrada inválida '%s' (formato zona=a.b.c.d)", tok);
            free(copy);
            return -1;
        }
        *eq = '\0';
        strcpy(z.name, tok);
        to_lower(z.name);
        cfg->zones[cfg->n_zones++] = z;
    }
    free(copy);
    return 0;
}

static int parse_int(const char *name, const char *text, int min, int max, int *out, char *err,
                     size_t errlen) {
    char *end;
    long v = strtol(text, &end, 10);
    if (*end != '\0' || v < min || v > max) {
        snprintf(err, errlen, "%s: valor inválido '%s' (debe estar entre %d y %d)", name, text, min,
                 max);
        return -1;
    }
    *out = (int)v;
    return 0;
}

int config_from_env(config_t *cfg, char *err, size_t errlen) {
    memset(cfg, 0, sizeof(*cfg));
    int v;

    if (parse_int("DI_PORT", env_or("DI_PORT", "5353"), 1, 65535, &cfg->port, err, errlen)) return -1;
    if (parse_int("DI_UPSTREAM_PORT", env_or("DI_UPSTREAM_PORT", "53"), 1, 65535,
                  &cfg->upstream_port, err, errlen))
        return -1;
    if (parse_int("DI_UPSTREAM_TIMEOUT_MS", env_or("DI_UPSTREAM_TIMEOUT_MS", "3000"), 100, 60000,
                  &cfg->upstream_timeout_ms, err, errlen))
        return -1;
    if (parse_int("DI_TTL", env_or("DI_TTL", "30"), 0, 86400, &v, err, errlen)) return -1;
    cfg->ttl = (uint32_t)v;

    snprintf(cfg->upstream_host, sizeof(cfg->upstream_host), "%s", env_or("DI_UPSTREAM_DNS", "8.8.8.8"));
    snprintf(cfg->country_db, sizeof(cfg->country_db), "%s",
             env_or("DI_COUNTRY_DB", "/app/data/dbip-country-lite.csv"));
    snprintf(cfg->zones_file, sizeof(cfg->zones_file), "%s",
             env_or("DI_ZONES_FILE", "/app/data/country_zones.csv"));
    snprintf(cfg->default_zone, sizeof(cfg->default_zone), "%s", env_or("DI_DEFAULT_ZONE", "america"));
    to_lower(cfg->default_zone);

    if (parse_domains(cfg, env_or("DI_DOMAINS", ""), err, errlen)) return -1;
    if (parse_zone_ips(cfg, env_or("DI_ZONE_IPS", ""), err, errlen)) return -1;

    uint32_t ignored;
    if (cfg->n_zones == 0) {
        snprintf(err, errlen, "DI_ZONE_IPS es obligatoria (formato zona=a.b.c.d,zona2=...)");
        return -1;
    }
    if (config_zone_ip(cfg, cfg->default_zone, &ignored) != 0) {
        snprintf(err, errlen, "DI_DEFAULT_ZONE '%s' no está definida en DI_ZONE_IPS", cfg->default_zone);
        return -1;
    }
    return 0;
}

int config_domain_match(const config_t *cfg, const char *name) {
    size_t nlen = strlen(name);
    for (size_t i = 0; i < cfg->n_domains; i++) {
        const char *p = cfg->domains[i];
        if (p[0] == '*' && p[1] == '.') {
            size_t slen = strlen(p + 1); /* ".sufijo" */
            if (nlen > slen && strcmp(name + nlen - slen, p + 1) == 0) return 1;
        } else if (strcmp(name, p) == 0) {
            return 1;
        }
    }
    return 0;
}

int config_zone_ip(const config_t *cfg, const char *zone, uint32_t *ip) {
    for (size_t i = 0; i < cfg->n_zones; i++) {
        if (strcmp(cfg->zones[i].name, zone) == 0) {
            *ip = cfg->zones[i].ip;
            return 0;
        }
    }
    return -1;
}
