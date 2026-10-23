/*
 * site_config.h - Modelo de la configuración de dominios y URLs administrada desde la UI
 * (guardada en Firebase y entregada a la caché por el API Node.js en formato JSON).
 *
 * Formato esperado (ver zonal-cache/README.md, sección "Contrato con el API Node.js"):
 * {
 *   "domains": [{
 *     "name": "apache.p2.local",            // admite comodines: "*.ejemplo.com"
 *     "origin": "http://apache:80",          // opcional; si falta se usa el Host
 *     "maxCacheSize": 10485760,              // bytes máximos en disco para el dominio
 *     "urls": [{
 *       "pattern": "/static/*",              // admite comodines '*' y '?'
 *       "cacheSize": 5242880,                // bytes máximos para esta URL
 *       "policy": "LRU",                     // LRU | LFU | FIFO | MRU | RANDOM
 *       "auth": "none",                      // none | api_key | user_password
 *       "fileTypes": [{ "extension": "png", "ttl": 3600 }],  // "*" = cualquier tipo
 *       "apiKeys": ["<sha256 en hexadecimal>"]
 *     }]
 *   }]
 * }
 */
#ifndef ZC_SITE_CONFIG_H
#define ZC_SITE_CONFIG_H

#include <stddef.h>

#include "policy.h"

typedef enum {
    AUTH_NONE = 0,
    AUTH_API_KEY,
    AUTH_USER_PASSWORD
} auth_mode_t;

typedef struct {
    char extension[16]; /* en minúsculas, sin punto; "*" = cualquiera */
    long ttl;           /* segundos */
} file_type_rule_t;

typedef struct {
    char *pattern;
    size_t cache_size;
    policy_type_t policy;
    auth_mode_t auth;
    file_type_rule_t *file_types;
    size_t file_type_count;
    char **api_key_hashes;
    size_t api_key_count;
} url_rule_t;

typedef struct {
    char *name;
    char *origin; /* NULL si no se definió */
    size_t max_cache_size;
    url_rule_t *rules;
    size_t rule_count;
} domain_rule_t;

typedef struct {
    domain_rule_t *domains;
    size_t domain_count;
} site_config_t;

/*
 * Convierte el JSON en una configuración. Devuelve NULL si es inválido y deja
 * un mensaje en err.
 */
site_config_t *site_config_parse(const char *json, char *err, size_t err_size);
void site_config_free(site_config_t *cfg);

/* Busca el dominio (el más específico si hay comodines). NULL si no se administra. */
const domain_rule_t *site_config_find_domain(const site_config_t *cfg, const char *host);

/* Busca la regla de URL que aplica a la ruta (la más específica). NULL si ninguna. */
const url_rule_t *domain_find_rule(const domain_rule_t *domain, const char *path);

/*
 * TTL en segundos para la extensión de la ruta según la regla, o -1 si ese tipo de
 * archivo no se debe guardar en caché. Las rutas sin extensión se tratan como "html".
 */
long url_rule_ttl_for_path(const url_rule_t *rule, const char *path);

int auth_mode_from_string(const char *s, auth_mode_t *out);
const char *auth_mode_to_string(auth_mode_t mode);

#endif
