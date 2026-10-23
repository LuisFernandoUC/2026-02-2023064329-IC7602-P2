/*
 * cache.h - Caché HTTP en disco con límites de tamaño por dominio y por URL.
 *
 * Organización:
 *   cache (raíz en ZC_CACHE_DIR)
 *     └── dominio      límite: maxCacheSize del dominio
 *           └── partición (una por patrón de URL)   límite: cacheSize + política propia
 *                 └── objetos (un archivo por respuesta guardada)
 *
 * Cada objeto se guarda en <dir>/<hash dominio>/<hash patrón>/<hash clave>-<id>.obj.
 * En memoria se mantiene un índice (clave -> archivo, tamaño, expiración) y el estado
 * de la política de reemplazo de cada partición. Todas las operaciones son seguras
 * entre hilos (un mutex global).
 */
#ifndef ZC_CACHE_H
#define ZC_CACHE_H

#include <stddef.h>
#include <time.h>

#include "policy.h"

typedef struct zc_cache zc_cache_t;

/* Dónde y con qué límites se guarda un objeto. */
typedef struct {
    const char *domain;
    size_t domain_max;    /* bytes máximos para todo el dominio */
    const char *partition;
    size_t partition_max; /* bytes máximos para la partición (URL) */
    policy_type_t policy;
} cache_scope_t;

typedef struct {
    int status;
    char content_type[128];
    unsigned char *body;
    size_t body_len;
    time_t stored_at;
    time_t expires_at;
} cache_object_t;

typedef enum {
    CACHE_STORED = 0,
    CACHE_TOO_LARGE = 1,  /* el objeto no cabe ni vaciando la partición/dominio */
    CACHE_ERROR = -1
} cache_put_result_t;

/*
 * Crea la caché en dir (se crea si no existe y se vacía su contenido previo,
 * porque el índice y la política viven en memoria). seed alimenta la política RANDOM.
 */
zc_cache_t *cache_create(const char *dir, unsigned int seed);
void cache_destroy(zc_cache_t *cache);

/*
 * Busca key. Devuelve 1 (hit, out queda lleno y debe liberarse con cache_object_free),
 * 0 (miss o expirado; los expirados se eliminan) o -1 (error de lectura).
 */
int cache_get(zc_cache_t *cache, const cache_scope_t *scope, const char *key, time_t now,
              cache_object_t *out);

/* Guarda (o reemplaza) key aplicando los límites y la política del scope. */
cache_put_result_t cache_put(zc_cache_t *cache, const cache_scope_t *scope, const char *key,
                             const cache_object_t *obj);

/* Elimina key si existe. Devuelve 1 si se eliminó. */
int cache_remove(zc_cache_t *cache, const cache_scope_t *scope, const char *key);

size_t cache_domain_usage(zc_cache_t *cache, const char *domain);
size_t cache_partition_usage(zc_cache_t *cache, const char *domain, const char *partition);
size_t cache_partition_count(zc_cache_t *cache, const char *domain, const char *partition);
int cache_contains(zc_cache_t *cache, const char *domain, const char *partition, const char *key);

void cache_object_free(cache_object_t *obj);

#endif
