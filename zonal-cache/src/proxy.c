/*
 * proxy.c - Implementación del flujo autenticación -> caché -> origen.
 */
#include "proxy.h"
#include "auth.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_URL 4096
#define MAX_TOKEN 4096

static void add_diag_headers(const proxy_t *px, http_response_t *resp, const char *cache_status)
{
    if (cache_status)
        http_response_add_header(resp, "X-Cache", cache_status);
    http_response_add_header(resp, "X-Zonal-Cache", px->zone ? px->zone : "default");
}

int proxy_is_safe_redirect(const char *next)
{
    if (!next || next[0] != '/')
        return 0;
    if (next[1] == '/' || next[1] == '\\')
        return 0; /* "//otro-sitio.com" sería una redirección externa */
    return strpbrk(next, "\r\n") == NULL;
}

/* URL pública del sitio tal como la ve el cliente: http(s)://Host */
static void public_base(const http_request_t *req, const request_ctx_t *rc, char *out,
                        size_t size)
{
    const char *host = http_request_header(req, "Host");
    snprintf(out, size, "%s://%s", rc->is_tls ? "https" : "http", host ? host : "");
}

/* ---------- autenticación ---------- */

static int extract_session_token(const http_request_t *req, char *out, size_t size,
                                 int *from_bearer)
{
    *from_bearer = 0;
    const char *authz = http_request_header(req, "Authorization");
    if (authz && strncmp(authz, "Bearer ", 7) == 0) {
        zc_strlcpy(out, authz + 7, size);
        *from_bearer = 1;
        return 1;
    }
    return http_request_cookie(req, AUTH_SESSION_COOKIE, out, size);
}

static void redirect_to_login(const proxy_t *px, const domain_rule_t *dom,
                              const http_request_t *req, const request_ctx_t *rc,
                              http_response_t *resp)
{
    char base[1024], callback[MAX_URL], location[MAX_URL * 2];
    public_base(req, rc, base, sizeof(base));
    char *next = zc_url_encode(req->target);
    snprintf(callback, sizeof(callback), "%s/__zc/auth?next=%s", base, next ? next : "%2F");
    free(next);

    char *enc_domain = zc_url_encode(dom->name);
    char *enc_callback = zc_url_encode(callback);
    snprintf(location, sizeof(location), "%s%cdomain=%s&callback=%s", px->login_url,
             strchr(px->login_url, '?') ? '&' : '?', enc_domain ? enc_domain : "",
             enc_callback ? enc_callback : "");
    free(enc_domain);
    free(enc_callback);
    http_response_redirect(resp, location);
}

/* Devuelve 0 si la petición puede continuar; si no, deja la respuesta de error en resp. */
static int check_auth(const proxy_t *px, const domain_rule_t *dom, const url_rule_t *rule,
                      const http_request_t *req, const request_ctx_t *rc, http_response_t *resp)
{
    if (rule->auth == AUTH_NONE)
        return 0;

    if (rule->auth == AUTH_API_KEY) {
        const char *key = http_request_header(req, "x-api-key");
        if (auth_api_key_valid(key, rule->api_key_hashes, rule->api_key_count))
            return 0;
        http_response_text(resp, 401, key ? "API key inválida\n" : "Falta el encabezado x-api-key\n");
        return -1;
    }

    char token[MAX_TOKEN];
    int from_bearer = 0;
    if (extract_session_token(req, token, sizeof(token), &from_bearer)) {
        token_status_t st = auth_token_verify(px->session_secret, token, dom->name, rc->now,
                                              NULL, 0, NULL);
        if (st == TOKEN_OK)
            return 0;
        if (from_bearer) {
            char msg[128];
            snprintf(msg, sizeof(msg), "Sesión inválida: %s\n", auth_token_status_text(st));
            http_response_text(resp, 401, msg);
            return -1;
        }
    }

    /* Navegador sin sesión válida: se envía al formulario de login de la UI. */
    if (px->login_url && px->login_url[0] &&
        (strcmp(req->method, "GET") == 0 || strcmp(req->method, "HEAD") == 0)) {
        redirect_to_login(px, dom, req, rc, resp);
    } else {
        http_response_text(resp, 401, "Se requiere iniciar sesión\n");
    }
    return -1;
}

/* GET /__zc/auth?token=<jwt>&next=<ruta>: la UI regresa aquí tras el login. */
static void handle_auth_callback(const proxy_t *px, const domain_rule_t *dom,
                                 const http_request_t *req, const request_ctx_t *rc,
                                 http_response_t *resp)
{
    char token[MAX_TOKEN], next[MAX_URL];
    if (!zc_query_param(req->query, "token", token, sizeof(token))) {
        http_response_text(resp, 400, "Falta el parámetro token\n");
        return;
    }
    if (!zc_query_param(req->query, "next", next, sizeof(next)) || !proxy_is_safe_redirect(next))
        zc_strlcpy(next, "/", sizeof(next));

    time_t exp = 0;
    token_status_t st = auth_token_verify(px->session_secret, token, dom->name, rc->now, NULL, 0,
                                          &exp);
    if (st != TOKEN_OK) {
        char msg[128];
        snprintf(msg, sizeof(msg), "No se pudo iniciar sesión: %s\n", auth_token_status_text(st));
        http_response_text(resp, 401, msg);
        return;
    }

    char cookie[MAX_TOKEN + 128];
    snprintf(cookie, sizeof(cookie), "%s=%s; Path=/; HttpOnly; SameSite=Lax; Max-Age=%lld%s",
             AUTH_SESSION_COOKIE, token, (long long)(exp - rc->now), rc->is_tls ? "; Secure" : "");
    http_response_redirect(resp, next);
    http_response_add_header(resp, "Set-Cookie", cookie);
}

/* GET /__zc/logout[?next=<ruta>]: elimina la cookie de sesión. */
static void handle_logout(const http_request_t *req, http_response_t *resp)
{
    char next[MAX_URL];
    if (zc_query_param(req->query, "next", next, sizeof(next)) && proxy_is_safe_redirect(next))
        http_response_redirect(resp, next);
    else
        http_response_text(resp, 200, "Sesión cerrada\n");
    http_response_add_header(resp, "Set-Cookie",
                             AUTH_SESSION_COOKIE "=; Path=/; HttpOnly; SameSite=Lax; Max-Age=0");
}

/* ---------- petición al origen ---------- */

typedef struct {
    http_header_t items[HTTP_MAX_HEADERS + 4];
    size_t count;
    char *owned[8];
    size_t owned_count;
} header_list_t;

static void header_list_add(header_list_t *l, const char *name, const char *value)
{
    if (l->count < sizeof(l->items) / sizeof(l->items[0])) {
        l->items[l->count].name = (char *)name;
        l->items[l->count].value = (char *)value;
        l->count++;
    }
}

static const char *header_list_own(header_list_t *l, char *s)
{
    if (s && l->owned_count < sizeof(l->owned) / sizeof(l->owned[0]))
        l->owned[l->owned_count++] = s;
    else if (s) {
        free(s);
        return NULL;
    }
    return s;
}

static void header_list_free(header_list_t *l)
{
    for (size_t i = 0; i < l->owned_count; i++)
        free(l->owned[i]);
}

/* Copia el encabezado Cookie sin la cookie de sesión de la caché. */
static char *strip_session_cookie(const char *cookies)
{
    char *out = calloc(1, strlen(cookies) + 1);
    if (!out)
        return NULL;
    const char *p = cookies;
    size_t name_len = strlen(AUTH_SESSION_COOKIE);
    while (*p) {
        while (*p == ' ' || *p == ';')
            p++;
        size_t seg = strcspn(p, ";");
        if (seg == 0)
            break;
        if (!(strncmp(p, AUTH_SESSION_COOKIE, name_len) == 0 && p[name_len] == '=')) {
            if (out[0])
                strcat(out, "; ");
            strncat(out, p, seg);
        }
        p += seg;
    }
    return out;
}

static void build_origin_headers(const http_request_t *req, const url_rule_t *rule,
                                 const request_ctx_t *rc, header_list_t *l)
{
    const char *xff = NULL;
    for (size_t i = 0; i < req->header_count; i++) {
        const char *name = req->headers[i].name;
        const char *value = req->headers[i].value;
        if (http_is_hop_by_hop(name) || zc_strcaseeq(name, "Host") ||
            zc_strcaseeq(name, "Content-Length") || zc_strcaseeq(name, "Accept-Encoding") ||
            zc_strcaseeq(name, "Expect") || zc_strcaseeq(name, "x-api-key") ||
            zc_strcaseeq(name, PROXY_LOOP_HEADER))
            continue;
        if (zc_strcaseeq(name, "X-Forwarded-For")) {
            xff = value;
            continue;
        }
        if (zc_strcaseeq(name, "Authorization") && rule && rule->auth == AUTH_USER_PASSWORD &&
            strncmp(value, "Bearer ", 7) == 0)
            continue; /* era el token de la caché, no del origen */
        if (zc_strcaseeq(name, "Cookie")) {
            const char *clean = header_list_own(l, strip_session_cookie(value));
            if (clean && clean[0])
                header_list_add(l, "Cookie", clean);
            continue;
        }
        header_list_add(l, name, value);
    }

    const char *ip = rc->client_ip ? rc->client_ip : "unknown";
    char *fwd = malloc((xff ? strlen(xff) : 0) + strlen(ip) + 3);
    if (fwd)
        sprintf(fwd, "%s%s%s", xff ? xff : "", xff ? ", " : "", ip);
    const char *fwd_owned = header_list_own(l, fwd);
    if (fwd_owned)
        header_list_add(l, "X-Forwarded-For", fwd_owned);
    header_list_add(l, "X-Forwarded-Proto", rc->is_tls ? "https" : "http");
    const char *host = http_request_header(req, "Host");
    if (host)
        header_list_add(l, "X-Forwarded-Host", host);
    header_list_add(l, PROXY_LOOP_HEADER, "1");
}

static int fetch_from_origin(const proxy_t *px, const domain_rule_t *dom, const url_rule_t *rule,
                             const http_request_t *req, const request_ctx_t *rc,
                             http_response_t *resp)
{
    char url[MAX_URL];
    if (dom->origin) {
        snprintf(url, sizeof(url), "%s%s", dom->origin, req->target);
    } else {
        char base[1024];
        public_base(req, rc, base, sizeof(base));
        snprintf(url, sizeof(url), "%s%s", base, req->target);
    }

    header_list_t headers = {0};
    build_origin_headers(req, rule, rc, &headers);
    origin_request_t oreq = {
        .method = req->method,
        .url = url,
        .headers = headers.items,
        .header_count = headers.count,
        .body = req->body,
        .body_len = req->body_len,
    };
    http_response_free(resp);
    http_response_init(resp, 502);
    int rc_fetch = px->fetch(px->fetch_ctx, &oreq, resp);
    header_list_free(&headers);
    if (rc_fetch != 0) {
        http_response_text(resp, 502, "No se pudo contactar el servidor de origen\n");
        return -1;
    }
    return 0;
}

/* ---------- flujo principal ---------- */

static void serve_from_cache(const proxy_t *px, const cache_object_t *obj, time_t now,
                             http_response_t *resp)
{
    http_response_free(resp);
    http_response_init(resp, obj->status);
    if (obj->content_type[0])
        http_response_add_header(resp, "Content-Type", obj->content_type);
    char age[32];
    snprintf(age, sizeof(age), "%lld", (long long)(now - obj->stored_at));
    http_response_add_header(resp, "Age", age);
    http_response_set_body(resp, obj->body, obj->body_len);
    add_diag_headers(px, resp, "HIT");
}

void proxy_handle(const proxy_t *px, const site_config_t *cfg, const http_request_t *req,
                  const request_ctx_t *rc, http_response_t *resp)
{
    http_response_init(resp, 200);

    if (strcmp(req->path, "/__zc/health") == 0) {
        http_response_text(resp, 200, "ok\n");
        add_diag_headers(px, resp, NULL);
        return;
    }
    if (http_request_header(req, PROXY_LOOP_HEADER)) {
        http_response_text(resp, 508, "Bucle detectado: la petición ya pasó por una caché zonal\n");
        return;
    }

    char host[256];
    if (http_request_host(req, host, sizeof(host)) != 0) {
        http_response_text(resp, 400, "Falta el encabezado Host\n");
        return;
    }
    const domain_rule_t *dom = site_config_find_domain(cfg, host);
    if (!dom) {
        http_response_text(resp, 404, "Dominio no administrado por esta caché\n");
        add_diag_headers(px, resp, NULL);
        return;
    }

    if (strcmp(req->path, "/__zc/auth") == 0) {
        handle_auth_callback(px, dom, req, rc, resp);
        return;
    }
    if (strcmp(req->path, "/__zc/logout") == 0) {
        handle_logout(req, resp);
        return;
    }

    const url_rule_t *rule = domain_find_rule(dom, req->path);
    if (rule && check_auth(px, dom, rule, req, rc, resp) != 0) {
        add_diag_headers(px, resp, NULL);
        return;
    }

    long ttl = rule ? url_rule_ttl_for_path(rule, req->path) : -1;
    int cacheable = rule && ttl > 0 && strcmp(req->method, "GET") == 0;

    char key[MAX_URL];
    snprintf(key, sizeof(key), "%s%s", host, req->target);
    cache_scope_t scope = {0};
    if (cacheable) {
        scope.domain = dom->name;
        scope.domain_max = dom->max_cache_size;
        scope.partition = rule->pattern;
        scope.partition_max = rule->cache_size;
        scope.policy = rule->policy;

        cache_object_t obj;
        if (cache_get(px->cache, &scope, key, rc->now, &obj) == 1) {
            serve_from_cache(px, &obj, rc->now, resp);
            cache_object_free(&obj);
            return;
        }
    }

    if (fetch_from_origin(px, dom, rule, req, rc, resp) != 0) {
        add_diag_headers(px, resp, NULL);
        return;
    }

    if (cacheable && resp->status == 200) {
        cache_object_t obj = {
            .status = resp->status,
            .body = resp->body,
            .body_len = resp->body_len,
            .stored_at = rc->now,
            .expires_at = rc->now + ttl,
        };
        const char *ctype = http_response_header(resp, "Content-Type");
        zc_strlcpy(obj.content_type, ctype ? ctype : "", sizeof(obj.content_type));
        cache_put(px->cache, &scope, key, &obj);
    }
    add_diag_headers(px, resp, cacheable ? "MISS" : "BYPASS");
}
