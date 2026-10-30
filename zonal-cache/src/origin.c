/*
 * origin.c - Peticiones salientes con libcurl (HTTP y HTTPS).
 */
#include "origin.h"
#include "util.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Buffer que crece según llegan los datos, con un tope (max) para no agotar memoria. */
typedef struct {
    unsigned char *data;
    size_t len;
    size_t cap;
    size_t max;
} buffer_t;

/* Callback de libcurl para el cuerpo. Devolver 0 hace que libcurl aborte. */
static size_t on_body(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    buffer_t *b = userdata;
    size_t n = size * nmemb;
    if (b->len + n > b->max)
        return 0; /* aborta la transferencia: respuesta demasiado grande */
    if (b->len + n + 1 > b->cap) {
        size_t new_cap = b->cap ? b->cap : 16384;
        while (new_cap < b->len + n + 1)
            new_cap *= 2;
        unsigned char *grown = realloc(b->data, new_cap);
        if (!grown)
            return 0;
        b->data = grown;
        b->cap = new_cap;
    }
    memcpy(b->data + b->len, ptr, n);
    b->len += n;
    b->data[b->len] = '\0';
    return n;
}

/*
 * Callback de libcurl: se llama una vez por cada línea de encabezado de la respuesta.
 * Se descartan los encabezados hop-by-hop y Content-Length (los define la caché).
 */
static size_t on_header(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    http_response_t *resp = userdata;
    size_t n = size * nmemb;
    char *line = zc_strndup(ptr, n);
    if (!line)
        return 0;
    line[strcspn(line, "\r\n")] = '\0';

    if (strncmp(line, "HTTP/", 5) == 0) {
        /* Nueva línea de estado (p. ej. después de un 100 Continue): reinicia. */
        int status = 0;
        const char *sp = strchr(line, ' ');
        if (sp)
            status = atoi(sp + 1);
        http_response_free(resp);
        http_response_init(resp, status);
    } else {
        char *colon = strchr(line, ':');
        if (colon) {
            *colon = '\0';
            const char *name = line;
            char *value = zc_str_trim(colon + 1);
            if (!http_is_hop_by_hop(name) && !zc_strcaseeq(name, "Content-Length"))
                http_response_add_header(resp, name, value);
        }
    }
    free(line);
    return n;
}

/*
 * Lista de encabezados para libcurl ("Nombre: valor"). Se agrega "Expect:" vacío
 * para que libcurl no espere un 100 Continue antes de enviar cuerpos grandes.
 */
static struct curl_slist *build_header_list(const origin_request_t *req)
{
    struct curl_slist *headers = NULL;
    for (size_t i = 0; i < req->header_count; i++) {
        size_t len = strlen(req->headers[i].name) + strlen(req->headers[i].value) + 3;
        char *h = malloc(len);
        if (!h)
            continue;
        snprintf(h, len, "%s: %s", req->headers[i].name, req->headers[i].value);
        headers = curl_slist_append(headers, h);
        free(h);
    }
    return curl_slist_append(headers, "Expect:");
}

/*
 * Reenvía la petición al origen sin seguir redirecciones (el 3xx se entrega tal
 * cual al cliente). El cuerpo de la respuesta se limita a opt->max_body bytes.
 */
int origin_fetch_curl(void *ctx, const origin_request_t *req, http_response_t *resp)
{
    const origin_options_t *opt = ctx;
    CURL *curl = curl_easy_init();
    if (!curl)
        return -1;

    struct curl_slist *headers = build_header_list(req);
    buffer_t body = {.max = opt->max_body};
    curl_easy_setopt(curl, CURLOPT_URL, req->url);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, req->method);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, opt->timeout_seconds);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, opt->timeout_seconds);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, opt->verify_tls ? 1L : 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, opt->verify_tls ? 2L : 0L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, on_body);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, on_header);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, resp);

    if (strcmp(req->method, "HEAD") == 0) {
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    } else if (req->body_len > 0 || strcmp(req->method, "POST") == 0 ||
               strcmp(req->method, "PUT") == 0 || strcmp(req->method, "PATCH") == 0) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, req->body ? (const char *)req->body : "");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)req->body_len);
    }

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK || resp->status == 0) {
        free(body.data);
        return -1;
    }
    free(resp->body);
    resp->body = body.data;
    resp->body_len = body.len;
    return 0;
}

char *origin_get_text(const char *url, const char *bearer, long timeout_seconds, char *err,
                      size_t err_size)
{
    CURL *curl = curl_easy_init();
    if (!curl) {
        snprintf(err, err_size, "no se pudo iniciar libcurl");
        return NULL;
    }
    struct curl_slist *headers = curl_slist_append(NULL, "Accept: application/json");
    if (bearer && *bearer) {
        size_t len = strlen(bearer) + 32;
        char *h = malloc(len);
        if (h) {
            snprintf(h, len, "Authorization: Bearer %s", bearer);
            headers = curl_slist_append(headers, h);
            free(h);
        }
    }

    buffer_t body = {.max = 32 * 1024 * 1024};
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_seconds);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, on_body);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);

    CURLcode res = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        snprintf(err, err_size, "%s", curl_easy_strerror(res));
        free(body.data);
        return NULL;
    }
    if (status != 200) {
        snprintf(err, err_size, "el API respondió %ld", status);
        free(body.data);
        return NULL;
    }
    if (!body.data)
        body.data = (unsigned char *)zc_strndup("", 0);
    return (char *)body.data;
}
