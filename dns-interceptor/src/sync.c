#include "sync.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_BODY_BYTES (1024 * 1024)
#define FETCH_TIMEOUT_SECONDS 10

typedef struct {
    char *data;
    size_t len;
} body_t;

static size_t on_body(char *chunk, size_t size, size_t nmemb, void *userp) {
    body_t *b = userp;
    size_t n = size * nmemb;
    if (b->len + n > MAX_BODY_BYTES) return 0; /* respuesta absurda: se aborta */
    char *grown = realloc(b->data, b->len + n + 1);
    if (!grown) return 0;
    memcpy(grown + b->len, chunk, n);
    b->len += n;
    grown[b->len] = '\0';
    b->data = grown;
    return n;
}

/* Descarga la lista del API. Devuelve 0 y deja la lista en out, o -1 y el motivo en err. */
static int fetch_domains(const config_t *cfg, domain_list_t *out, char *err, size_t errlen) {
    CURL *curl = curl_easy_init();
    if (!curl) {
        snprintf(err, errlen, "no se pudo iniciar libcurl");
        return -1;
    }
    body_t body = {0};
    struct curl_slist *headers = curl_slist_append(NULL, "Accept: application/json");
    if (cfg->domains_token[0]) {
        char auth[CFG_URL_LEN + 32];
        snprintf(auth, sizeof(auth), "Authorization: Bearer %s", cfg->domains_token);
        headers = curl_slist_append(headers, auth);
    }
    curl_easy_setopt(curl, CURLOPT_URL, cfg->domains_url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)FETCH_TIMEOUT_SECONDS);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, on_body);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);

    CURLcode res = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    int rc = -1;
    if (res != CURLE_OK)
        snprintf(err, errlen, "%s", curl_easy_strerror(res));
    else if (status != 200)
        snprintf(err, errlen, "el API respondió HTTP %ld", status);
    else if (!body.data || domains_parse_json(body.data, out) != 0)
        snprintf(err, errlen, "respuesta del API con formato inválido");
    else
        rc = 0;
    free(body.data);
    return rc;
}

static void sleep_seconds(int seconds, volatile sig_atomic_t *stop) {
    struct timespec ts = {.tv_sec = 0, .tv_nsec = 200 * 1000 * 1000};
    for (int i = 0; i < seconds * 5 && !*stop; i++) nanosleep(&ts, NULL);
}

void *domains_sync_loop(void *arg) {
    sync_ctx_t *ctx = arg;
    int failing = 0; /* para avisar una sola vez por cada racha de fallos */
    curl_global_init(CURL_GLOBAL_DEFAULT);

    while (!*ctx->stop) {
        domain_list_t list;
        char err[160];
        if (fetch_domains(ctx->cfg, &list, err, sizeof(err)) == 0) {
            if (failing) printf("API de dominios recuperada\n");
            failing = 0;
            if (dyn_domains_replace(ctx->dyn, &list))
                printf("dominios del API actualizados: %zu\n", list.count);
        } else if (!failing) {
            failing = 1;
            printf("no se pudo consultar el API de dominios (%s); se conserva la última lista (%zu)\n",
                   err, dyn_domains_count(ctx->dyn));
        }
        fflush(stdout);
        sleep_seconds(ctx->cfg->domains_refresh_seconds, ctx->stop);
    }
    return NULL;
}
