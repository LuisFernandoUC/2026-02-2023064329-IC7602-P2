/*
 * proxy.h - Lógica de atención de una petición en la Zonal Cache.
 *
 * Flujo (independiente de sockets y de TLS, para poder probarlo con un origen falso):
 *   1. Endpoints internos: /__zc/health, /__zc/auth (retorno del login), /__zc/logout.
 *   2. Se identifica el dominio por el encabezado Host y la regla de URL por la ruta.
 *   3. Si la regla exige seguridad se valida x-api-key o la sesión (JWT).
 *   4. Si el recurso es cacheable (GET + tipo de archivo con TTL) se busca en disco.
 *   5. Si no está, se pide al origen, se guarda (si es 200) y se responde.
 * Encabezados de diagnóstico: X-Cache (HIT | MISS | BYPASS) y X-Zonal-Cache (zona).
 */
#ifndef ZC_PROXY_H
#define ZC_PROXY_H

#include <stddef.h>
#include <time.h>

#include "cache.h"
#include "http.h"
#include "site_config.h"

#define PROXY_LOOP_HEADER "X-Zonal-Cache-Hop"

typedef struct {
    const char *method;
    const char *url;
    const http_header_t *headers;
    size_t header_count;
    const unsigned char *body;
    size_t body_len;
} origin_request_t;

/*
 * Pide el recurso al origen. Debe llenar resp (status, encabezados sin hop-by-hop,
 * cuerpo). Devuelve 0 si obtuvo respuesta o -1 si el origen no respondió.
 */
typedef int (*origin_fetch_fn)(void *ctx, const origin_request_t *req, http_response_t *resp);

typedef struct {
    zc_cache_t *cache;
    const char *zone;
    const char *login_url;      /* formulario de la UI; "" = deshabilitado */
    const char *session_secret; /* secreto HS256 compartido con el API Node.js */
    origin_fetch_fn fetch;
    void *fetch_ctx;
} proxy_t;

typedef struct {
    const char *client_ip;
    int is_tls;
    time_t now;
} request_ctx_t;

/* Atiende req y deja la respuesta en resp (que debe liberarse con http_response_free). */
void proxy_handle(const proxy_t *px, const site_config_t *cfg, const http_request_t *req,
                  const request_ctx_t *rc, http_response_t *resp);

/* Devuelve 1 si next es una ruta local segura para redirigir (evita open redirects). */
int proxy_is_safe_redirect(const char *next);

#endif
