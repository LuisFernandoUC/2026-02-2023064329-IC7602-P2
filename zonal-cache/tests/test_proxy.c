/*
 * test_proxy.c - Pruebas del flujo completo con un servidor de origen falso
 * (sin red): autenticación, HIT/MISS/BYPASS, login por la UI y reenvío al origen.
 */
#include "test.h"
#include "../src/auth.h"
#include "../src/proxy.h"

#include <stdlib.h>

#define SECRET "secreto-de-prueba"
#define LOGIN_URL "https://ui.example.com/login"
/* SHA-256 de "clave-valida" */
#define KEY_HASH "e2bc022cb9c8b0dfd4db95058c79f8422bdecec430dd3907c945509a6c0926bc"

static const char *CONFIG_JSON =
    "{ \"domains\": ["
    "  { \"name\": \"sitio.local\", \"origin\": \"http://origen:80\", \"maxCacheSize\": 100000,"
    "    \"urls\": ["
    "      { \"pattern\": \"/*\", \"cacheSize\": 50000, \"policy\": \"LRU\", \"auth\": \"none\","
    "        \"fileTypes\": [{\"extension\": \"html\", \"ttl\": 60}, {\"extension\": \"png\", \"ttl\": 60}] },"
    "      { \"pattern\": \"/privado/*\", \"cacheSize\": 50000, \"policy\": \"FIFO\", \"auth\": \"user_password\","
    "        \"fileTypes\": [{\"extension\": \"*\", \"ttl\": 60}] },"
    "      { \"pattern\": \"/api/*\", \"cacheSize\": 50000, \"auth\": \"api_key\","
    "        \"apiKeys\": [\"" KEY_HASH "\"] }"
    "    ] },"
    "  { \"name\": \"sin-origen.local\", \"maxCacheSize\": 1000,"
    "    \"urls\": [{ \"pattern\": \"/*\", \"cacheSize\": 1000, \"auth\": \"none\" }] }"
    "] }";

typedef struct {
    int calls;
    int status;
    int fail;
    char url[512];
    char method[16];
    char headers[2048]; /* "Nombre: valor\n" ... */
    char body[256];
} fake_origin_t;

static int fake_fetch(void *ctx, const origin_request_t *req, http_response_t *resp)
{
    fake_origin_t *o = ctx;
    o->calls++;
    if (o->fail)
        return -1;
    snprintf(o->url, sizeof(o->url), "%s", req->url);
    snprintf(o->method, sizeof(o->method), "%s", req->method);
    o->headers[0] = '\0';
    for (size_t i = 0; i < req->header_count; i++) {
        size_t len = strlen(o->headers);
        snprintf(o->headers + len, sizeof(o->headers) - len, "%s: %s\n", req->headers[i].name,
                 req->headers[i].value);
    }
    snprintf(o->body, sizeof(o->body), "%.*s", (int)req->body_len, req->body ? (const char *)req->body : "");

    http_response_init(resp, o->status);
    http_response_add_header(resp, "Content-Type", "text/html");
    char body[64];
    snprintf(body, sizeof(body), "respuesta #%d", o->calls);
    http_response_set_body(resp, body, strlen(body));
    return 0;
}

typedef struct {
    char dir[256];
    zc_cache_t *cache;
    site_config_t *cfg;
    fake_origin_t origin;
    proxy_t proxy;
} env_t;

static void env_init(env_t *e)
{
    memset(e, 0, sizeof(*e));
    test_tmpdir(e->dir, sizeof(e->dir));
    e->cache = cache_create(e->dir, 1);
    e->cfg = site_config_parse(CONFIG_JSON, NULL, 0);
    e->origin.status = 200;
    e->proxy = (proxy_t){
        .cache = e->cache,
        .zone = "america",
        .login_url = LOGIN_URL,
        .session_secret = SECRET,
        .fetch = fake_fetch,
        .fetch_ctx = &e->origin,
    };
}

static void env_free(env_t *e)
{
    site_config_free(e->cfg);
    cache_destroy(e->cache);
}

/* Ejecuta una petición cruda contra el proxy. */
static void request(env_t *e, const char *raw, int is_tls, http_response_t *resp)
{
    http_request_t req;
    long head = http_parse_request_head(raw, strlen(raw), &req);
    if (head <= 0) {
        fprintf(stderr, "    petición de prueba inválida: %s\n", raw);
        test_current_failed = 1;
        http_response_init(resp, 0);
        return;
    }
    size_t body_len = strlen(raw) - (size_t)head;
    http_request_set_body(&req, (const unsigned char *)raw + head, body_len);
    request_ctx_t rc = {.client_ip = "10.0.0.7", .is_tls = is_tls, .now = 1000};
    proxy_handle(&e->proxy, e->cfg, &req, &rc, resp);
    http_request_free(&req);
}

static int body_is(const http_response_t *resp, const char *expected)
{
    return resp->body_len == strlen(expected) && memcmp(resp->body, expected, resp->body_len) == 0;
}

TEST(health_endpoint)
{
    env_t e;
    env_init(&e);
    http_response_t r;
    request(&e, "GET /__zc/health HTTP/1.1\r\n\r\n", 0, &r);
    CHECK_EQ_INT(r.status, 200);
    CHECK_EQ_STR(http_response_header(&r, "X-Zonal-Cache"), "america");
    http_response_free(&r);
    env_free(&e);
}

TEST(missing_host_and_unknown_domain)
{
    env_t e;
    env_init(&e);
    http_response_t r;
    request(&e, "GET / HTTP/1.1\r\n\r\n", 0, &r);
    CHECK_EQ_INT(r.status, 400);
    http_response_free(&r);
    request(&e, "GET / HTTP/1.1\r\nHost: otro.com\r\n\r\n", 0, &r);
    CHECK_EQ_INT(r.status, 404);
    http_response_free(&r);
    CHECK_EQ_INT(e.origin.calls, 0);
    env_free(&e);
}

TEST(miss_then_hit)
{
    env_t e;
    env_init(&e);
    http_response_t r;
    request(&e, "GET /index.html HTTP/1.1\r\nHost: sitio.local\r\n\r\n", 0, &r);
    CHECK_EQ_INT(r.status, 200);
    CHECK_EQ_STR(http_response_header(&r, "X-Cache"), "MISS");
    CHECK(body_is(&r, "respuesta #1"));
    CHECK_EQ_STR(e.origin.url, "http://origen:80/index.html");
    http_response_free(&r);

    request(&e, "GET /index.html HTTP/1.1\r\nHost: SITIO.local:8080\r\n\r\n", 0, &r);
    CHECK_EQ_INT(r.status, 200);
    CHECK_EQ_STR(http_response_header(&r, "X-Cache"), "HIT");
    CHECK_EQ_STR(http_response_header(&r, "Content-Type"), "text/html");
    CHECK_EQ_STR(http_response_header(&r, "Age"), "0");
    CHECK(body_is(&r, "respuesta #1"));
    CHECK_EQ_INT(e.origin.calls, 1);
    http_response_free(&r);
    env_free(&e);
}

TEST(query_string_is_part_of_key)
{
    env_t e;
    env_init(&e);
    http_response_t r;
    request(&e, "GET /a.html?v=1 HTTP/1.1\r\nHost: sitio.local\r\n\r\n", 0, &r);
    http_response_free(&r);
    request(&e, "GET /a.html?v=2 HTTP/1.1\r\nHost: sitio.local\r\n\r\n", 0, &r);
    CHECK_EQ_STR(http_response_header(&r, "X-Cache"), "MISS");
    http_response_free(&r);
    CHECK_EQ_INT(e.origin.calls, 2);
    env_free(&e);
}

TEST(non_get_and_unconfigured_types_bypass_cache)
{
    env_t e;
    env_init(&e);
    http_response_t r;
    const char *post = "POST /index.html HTTP/1.1\r\nHost: sitio.local\r\nContent-Length: 5\r\n\r\nhola!";
    request(&e, post, 0, &r);
    CHECK_EQ_STR(http_response_header(&r, "X-Cache"), "BYPASS");
    CHECK_EQ_STR(e.origin.method, "POST");
    CHECK_EQ_STR(e.origin.body, "hola!");
    http_response_free(&r);
    request(&e, post, 0, &r);
    http_response_free(&r);
    CHECK_EQ_INT(e.origin.calls, 2);

    request(&e, "GET /estilos.css HTTP/1.1\r\nHost: sitio.local\r\n\r\n", 0, &r);
    CHECK_EQ_STR(http_response_header(&r, "X-Cache"), "BYPASS");
    http_response_free(&r);
    request(&e, "GET /estilos.css HTTP/1.1\r\nHost: sitio.local\r\n\r\n", 0, &r);
    http_response_free(&r);
    CHECK_EQ_INT(e.origin.calls, 4);
    env_free(&e);
}

TEST(error_responses_are_not_cached)
{
    env_t e;
    env_init(&e);
    e.origin.status = 404;
    http_response_t r;
    request(&e, "GET /no.html HTTP/1.1\r\nHost: sitio.local\r\n\r\n", 0, &r);
    CHECK_EQ_INT(r.status, 404);
    http_response_free(&r);
    request(&e, "GET /no.html HTTP/1.1\r\nHost: sitio.local\r\n\r\n", 0, &r);
    CHECK_EQ_STR(http_response_header(&r, "X-Cache"), "MISS");
    http_response_free(&r);
    CHECK_EQ_INT(e.origin.calls, 2);
    env_free(&e);
}

TEST(origin_failure_returns_502)
{
    env_t e;
    env_init(&e);
    e.origin.fail = 1;
    http_response_t r;
    request(&e, "GET /index.html HTTP/1.1\r\nHost: sitio.local\r\n\r\n", 0, &r);
    CHECK_EQ_INT(r.status, 502);
    http_response_free(&r);
    env_free(&e);
}

TEST(api_key_authentication)
{
    env_t e;
    env_init(&e);
    http_response_t r;
    request(&e, "GET /api/items HTTP/1.1\r\nHost: sitio.local\r\n\r\n", 0, &r);
    CHECK_EQ_INT(r.status, 401);
    http_response_free(&r);
    request(&e, "GET /api/items HTTP/1.1\r\nHost: sitio.local\r\nx-api-key: mala\r\n\r\n", 0, &r);
    CHECK_EQ_INT(r.status, 401);
    http_response_free(&r);
    CHECK_EQ_INT(e.origin.calls, 0);

    request(&e, "DELETE /api/items/3 HTTP/1.1\r\nHost: sitio.local\r\nX-API-Key: clave-valida\r\n\r\n",
            0, &r);
    CHECK_EQ_INT(r.status, 200);
    CHECK_EQ_STR(e.origin.method, "DELETE");
    CHECK(strstr(e.origin.headers, "clave-valida") == NULL); /* la clave no llega al origen */
    http_response_free(&r);
    env_free(&e);
}

TEST(user_password_redirects_to_login)
{
    env_t e;
    env_init(&e);
    http_response_t r;
    request(&e, "GET /privado/doc.html?x=1 HTTP/1.1\r\nHost: sitio.local:8080\r\n\r\n", 0, &r);
    CHECK_EQ_INT(r.status, 302);
    const char *loc = http_response_header(&r, "Location");
    CHECK_EQ_STR(loc, LOGIN_URL "?domain=sitio.local&callback="
                      "http%3A%2F%2Fsitio.local%3A8080%2F__zc%2Fauth%3Fnext%3D"
                      "%252Fprivado%252Fdoc.html%253Fx%253D1");
    http_response_free(&r);

    /* Clientes que no son navegador (no GET) reciben 401. */
    request(&e, "PUT /privado/doc.html HTTP/1.1\r\nHost: sitio.local\r\n\r\n", 0, &r);
    CHECK_EQ_INT(r.status, 401);
    http_response_free(&r);
    CHECK_EQ_INT(e.origin.calls, 0);
    env_free(&e);
}

TEST(user_password_accepts_valid_session)
{
    env_t e;
    env_init(&e);
    char *tok = auth_token_create(SECRET, "sitio.local", "ana", 5000);
    char raw[2048];
    snprintf(raw, sizeof(raw),
             "GET /privado/doc.html HTTP/1.1\r\nHost: sitio.local\r\nCookie: tema=oscuro; %s=%s\r\n\r\n",
             AUTH_SESSION_COOKIE, tok);
    http_response_t r;
    request(&e, raw, 0, &r);
    CHECK_EQ_INT(r.status, 200);
    CHECK(strstr(e.origin.headers, "Cookie: tema=oscuro\n") != NULL);
    CHECK(strstr(e.origin.headers, AUTH_SESSION_COOKIE) == NULL);
    http_response_free(&r);

    snprintf(raw, sizeof(raw),
             "GET /privado/otro.html HTTP/1.1\r\nHost: sitio.local\r\nAuthorization: Bearer %s\r\n\r\n",
             tok);
    request(&e, raw, 0, &r);
    CHECK_EQ_INT(r.status, 200);
    CHECK(strstr(e.origin.headers, "Authorization") == NULL);
    http_response_free(&r);
    free(tok);
    env_free(&e);
}

TEST(user_password_rejects_bad_sessions)
{
    env_t e;
    env_init(&e);
    http_response_t r;
    char raw[2048];

    char *expired = auth_token_create(SECRET, "sitio.local", "ana", 999); /* now = 1000 */
    snprintf(raw, sizeof(raw), "GET /privado/a.html HTTP/1.1\r\nHost: sitio.local\r\nCookie: %s=%s\r\n\r\n",
             AUTH_SESSION_COOKIE, expired);
    request(&e, raw, 0, &r);
    CHECK_EQ_INT(r.status, 302); /* se vuelve a pedir login */
    http_response_free(&r);

    char *other = auth_token_create(SECRET, "otro.local", "ana", 5000);
    snprintf(raw, sizeof(raw), "GET /privado/a.html HTTP/1.1\r\nHost: sitio.local\r\nAuthorization: Bearer %s\r\n\r\n",
             other);
    request(&e, raw, 0, &r);
    CHECK_EQ_INT(r.status, 401);
    http_response_free(&r);
    CHECK_EQ_INT(e.origin.calls, 0);
    free(expired);
    free(other);
    env_free(&e);
}

TEST(login_callback_sets_cookie)
{
    env_t e;
    env_init(&e);
    char *tok = auth_token_create(SECRET, "sitio.local", "ana", 4600);
    char raw[2048];
    snprintf(raw, sizeof(raw),
             "GET /__zc/auth?next=%%2Fprivado%%2Fdoc.html&token=%s HTTP/1.1\r\nHost: sitio.local\r\n\r\n",
             tok);
    http_response_t r;
    request(&e, raw, 1, &r);
    CHECK_EQ_INT(r.status, 302);
    CHECK_EQ_STR(http_response_header(&r, "Location"), "/privado/doc.html");
    const char *cookie = http_response_header(&r, "Set-Cookie");
    CHECK(cookie && strncmp(cookie, AUTH_SESSION_COOKIE "=", strlen(AUTH_SESSION_COOKIE) + 1) == 0);
    CHECK(cookie && strstr(cookie, tok) != NULL);
    CHECK(cookie && strstr(cookie, "HttpOnly") != NULL);
    CHECK(cookie && strstr(cookie, "Max-Age=3600") != NULL);
    CHECK(cookie && strstr(cookie, "Secure") != NULL); /* llegó por HTTPS */
    http_response_free(&r);

    /* next externo: se ignora y se redirige a "/" */
    snprintf(raw, sizeof(raw),
             "GET /__zc/auth?token=%s&next=%%2F%%2Fevil.com HTTP/1.1\r\nHost: sitio.local\r\n\r\n", tok);
    request(&e, raw, 0, &r);
    CHECK_EQ_STR(http_response_header(&r, "Location"), "/");
    http_response_free(&r);

    request(&e, "GET /__zc/auth?token=basura HTTP/1.1\r\nHost: sitio.local\r\n\r\n", 0, &r);
    CHECK_EQ_INT(r.status, 401);
    CHECK(http_response_header(&r, "Set-Cookie") == NULL);
    http_response_free(&r);
    request(&e, "GET /__zc/auth HTTP/1.1\r\nHost: sitio.local\r\n\r\n", 0, &r);
    CHECK_EQ_INT(r.status, 400);
    http_response_free(&r);
    free(tok);
    env_free(&e);
}

TEST(logout_clears_cookie)
{
    env_t e;
    env_init(&e);
    http_response_t r;
    request(&e, "GET /__zc/logout HTTP/1.1\r\nHost: sitio.local\r\n\r\n", 0, &r);
    CHECK_EQ_INT(r.status, 200);
    const char *cookie = http_response_header(&r, "Set-Cookie");
    CHECK(cookie && strstr(cookie, "Max-Age=0") != NULL);
    http_response_free(&r);
    request(&e, "GET /__zc/logout?next=%2F HTTP/1.1\r\nHost: sitio.local\r\n\r\n", 0, &r);
    CHECK_EQ_INT(r.status, 302);
    http_response_free(&r);
    env_free(&e);
}

TEST(origin_from_host_when_not_configured)
{
    env_t e;
    env_init(&e);
    http_response_t r;
    request(&e, "GET /x?y=1 HTTP/1.1\r\nHost: sin-origen.local:8443\r\n\r\n", 1, &r);
    CHECK_EQ_STR(e.origin.url, "https://sin-origen.local:8443/x?y=1");
    http_response_free(&r);
    env_free(&e);
}

TEST(forwarding_headers)
{
    env_t e;
    env_init(&e);
    http_response_t r;
    request(&e,
            "GET /c.css HTTP/1.1\r\nHost: sitio.local\r\nX-Forwarded-For: 1.2.3.4\r\n"
            "Connection: keep-alive\r\nAccept-Encoding: gzip\r\nUser-Agent: curl\r\n\r\n",
            0, &r);
    CHECK(strstr(e.origin.headers, "X-Forwarded-For: 1.2.3.4, 10.0.0.7\n") != NULL);
    CHECK(strstr(e.origin.headers, "X-Forwarded-Proto: http\n") != NULL);
    CHECK(strstr(e.origin.headers, "X-Forwarded-Host: sitio.local\n") != NULL);
    CHECK(strstr(e.origin.headers, "User-Agent: curl\n") != NULL);
    CHECK(strstr(e.origin.headers, "Connection") == NULL);
    CHECK(strstr(e.origin.headers, "Accept-Encoding") == NULL);
    CHECK(strstr(e.origin.headers, PROXY_LOOP_HEADER ": 1\n") != NULL);
    http_response_free(&r);
    env_free(&e);
}

TEST(loop_is_detected)
{
    env_t e;
    env_init(&e);
    http_response_t r;
    request(&e, "GET / HTTP/1.1\r\nHost: sitio.local\r\n" PROXY_LOOP_HEADER ": 1\r\n\r\n", 0, &r);
    CHECK_EQ_INT(r.status, 508);
    CHECK_EQ_INT(e.origin.calls, 0);
    http_response_free(&r);
    env_free(&e);
}

TEST(safe_redirects)
{
    CHECK(proxy_is_safe_redirect("/"));
    CHECK(proxy_is_safe_redirect("/a/b?c=d"));
    CHECK(!proxy_is_safe_redirect("//evil.com"));
    CHECK(!proxy_is_safe_redirect("/\\evil.com"));
    CHECK(!proxy_is_safe_redirect("https://evil.com"));
    CHECK(!proxy_is_safe_redirect("/a\r\nSet-Cookie: x"));
    CHECK(!proxy_is_safe_redirect(NULL));
}

void suite_proxy(void)
{
    RUN(health_endpoint);
    RUN(missing_host_and_unknown_domain);
    RUN(miss_then_hit);
    RUN(query_string_is_part_of_key);
    RUN(non_get_and_unconfigured_types_bypass_cache);
    RUN(error_responses_are_not_cached);
    RUN(origin_failure_returns_502);
    RUN(api_key_authentication);
    RUN(user_password_redirects_to_login);
    RUN(user_password_accepts_valid_session);
    RUN(user_password_rejects_bad_sessions);
    RUN(login_callback_sets_cookie);
    RUN(logout_clears_cookie);
    RUN(origin_from_host_when_not_configured);
    RUN(forwarding_headers);
    RUN(loop_is_detected);
    RUN(safe_redirects);
}
