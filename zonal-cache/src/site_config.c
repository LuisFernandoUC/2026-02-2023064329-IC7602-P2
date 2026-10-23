/*
 * site_config.c - Lectura del JSON de configuración con cJSON y búsqueda de reglas.
 */
#include "site_config.h"
#include "util.h"
#include "wildcard.h"

#include <cjson/cJSON.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_err(char *err, size_t err_size, const char *fmt, ...)
{
    if (!err || err_size == 0)
        return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, err_size, fmt, ap);
    va_end(ap);
}

static char *dup_json_string(const cJSON *obj, const char *name)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, name);
    if (!cJSON_IsString(item) || !item->valuestring || !item->valuestring[0])
        return NULL;
    return zc_strndup(item->valuestring, strlen(item->valuestring));
}

/* Lee un número no negativo; acepta también cadenas numéricas ("1048576"). */
static int read_size(const cJSON *obj, const char *name, double *out)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, name);
    if (cJSON_IsNumber(item)) {
        *out = item->valuedouble;
    } else if (cJSON_IsString(item) && item->valuestring) {
        char *endp = NULL;
        *out = strtod(item->valuestring, &endp);
        if (endp == item->valuestring || *endp != '\0')
            return -1;
    } else {
        return -1;
    }
    return *out >= 0 ? 0 : -1;
}

int auth_mode_from_string(const char *s, auth_mode_t *out)
{
    if (!s || zc_strcaseeq(s, "none") || !*s) {
        *out = AUTH_NONE;
    } else if (zc_strcaseeq(s, "api_key") || zc_strcaseeq(s, "apikey") ||
               zc_strcaseeq(s, "x-api-key")) {
        *out = AUTH_API_KEY;
    } else if (zc_strcaseeq(s, "user_password") || zc_strcaseeq(s, "userpass") ||
               zc_strcaseeq(s, "password")) {
        *out = AUTH_USER_PASSWORD;
    } else {
        return -1;
    }
    return 0;
}

const char *auth_mode_to_string(auth_mode_t mode)
{
    switch (mode) {
    case AUTH_API_KEY:
        return "api_key";
    case AUTH_USER_PASSWORD:
        return "user_password";
    default:
        return "none";
    }
}

static void free_rule(url_rule_t *r)
{
    free(r->pattern);
    free(r->file_types);
    for (size_t i = 0; i < r->api_key_count; i++)
        free(r->api_key_hashes[i]);
    free(r->api_key_hashes);
}

static void free_domain(domain_rule_t *d)
{
    free(d->name);
    free(d->origin);
    for (size_t i = 0; i < d->rule_count; i++)
        free_rule(&d->rules[i]);
    free(d->rules);
}

void site_config_free(site_config_t *cfg)
{
    if (!cfg)
        return;
    for (size_t i = 0; i < cfg->domain_count; i++)
        free_domain(&cfg->domains[i]);
    free(cfg->domains);
    free(cfg);
}

static int parse_rule(const cJSON *json, url_rule_t *r, const char *domain, char *err,
                      size_t err_size)
{
    r->pattern = dup_json_string(json, "pattern");
    if (!r->pattern) {
        set_err(err, err_size, "dominio %s: una URL no tiene 'pattern'", domain);
        return -1;
    }

    double size = 0;
    if (read_size(json, "cacheSize", &size) != 0) {
        set_err(err, err_size, "%s%s: 'cacheSize' inválido", domain, r->pattern);
        return -1;
    }
    r->cache_size = (size_t)size;

    const cJSON *policy = cJSON_GetObjectItemCaseSensitive(json, "policy");
    r->policy = POLICY_LRU;
    if (policy && (!cJSON_IsString(policy) || policy_from_string(policy->valuestring, &r->policy) != 0)) {
        set_err(err, err_size, "%s%s: política desconocida", domain, r->pattern);
        return -1;
    }

    const cJSON *auth = cJSON_GetObjectItemCaseSensitive(json, "auth");
    const char *auth_str = cJSON_IsString(auth) ? auth->valuestring : NULL;
    if ((auth && !cJSON_IsString(auth) && !cJSON_IsNull(auth)) ||
        auth_mode_from_string(auth_str, &r->auth) != 0) {
        set_err(err, err_size, "%s%s: mecanismo de autenticación desconocido", domain, r->pattern);
        return -1;
    }

    const cJSON *types = cJSON_GetObjectItemCaseSensitive(json, "fileTypes");
    if (cJSON_IsArray(types)) {
        int n = cJSON_GetArraySize(types);
        r->file_types = calloc((size_t)(n > 0 ? n : 1), sizeof(*r->file_types));
        if (!r->file_types)
            return -1;
        const cJSON *t;
        cJSON_ArrayForEach(t, types) {
            const cJSON *ext = cJSON_GetObjectItemCaseSensitive(t, "extension");
            double ttl = 0;
            if (!cJSON_IsString(ext) || !ext->valuestring || read_size(t, "ttl", &ttl) != 0) {
                set_err(err, err_size, "%s%s: 'fileTypes' inválido", domain, r->pattern);
                return -1;
            }
            file_type_rule_t *ft = &r->file_types[r->file_type_count++];
            const char *e = ext->valuestring[0] == '.' ? ext->valuestring + 1 : ext->valuestring;
            zc_strlcpy(ft->extension, e, sizeof(ft->extension));
            zc_str_tolower(ft->extension);
            ft->ttl = (long)ttl;
        }
    }

    const cJSON *keys = cJSON_GetObjectItemCaseSensitive(json, "apiKeys");
    if (cJSON_IsArray(keys)) {
        int n = cJSON_GetArraySize(keys);
        r->api_key_hashes = calloc((size_t)(n > 0 ? n : 1), sizeof(char *));
        if (!r->api_key_hashes)
            return -1;
        const cJSON *k;
        cJSON_ArrayForEach(k, keys) {
            /* Se acepta "hash" como cadena o como objeto { "hash": "..." }. */
            const char *h = cJSON_IsString(k) ? k->valuestring : NULL;
            if (cJSON_IsObject(k)) {
                const cJSON *hv = cJSON_GetObjectItemCaseSensitive(k, "hash");
                h = cJSON_IsString(hv) ? hv->valuestring : NULL;
            }
            if (!h)
                continue;
            char *copy = zc_strndup(h, strlen(h));
            if (!copy)
                return -1;
            zc_str_tolower(copy);
            r->api_key_hashes[r->api_key_count++] = copy;
        }
    }
    return 0;
}

static int parse_domain(const cJSON *json, domain_rule_t *d, char *err, size_t err_size)
{
    d->name = dup_json_string(json, "name");
    if (!d->name)
        d->name = dup_json_string(json, "domain");
    if (!d->name) {
        set_err(err, err_size, "un dominio no tiene 'name'");
        return -1;
    }
    zc_str_tolower(d->name);

    d->origin = dup_json_string(json, "origin");
    if (d->origin) {
        size_t len = strlen(d->origin);
        while (len > 0 && d->origin[len - 1] == '/')
            d->origin[--len] = '\0';
    }

    double size = 0;
    if (read_size(json, "maxCacheSize", &size) != 0) {
        set_err(err, err_size, "dominio %s: 'maxCacheSize' inválido", d->name);
        return -1;
    }
    d->max_cache_size = (size_t)size;

    const cJSON *urls = cJSON_GetObjectItemCaseSensitive(json, "urls");
    if (!cJSON_IsArray(urls))
        return 0;
    int n = cJSON_GetArraySize(urls);
    d->rules = calloc((size_t)(n > 0 ? n : 1), sizeof(*d->rules));
    if (!d->rules)
        return -1;
    const cJSON *u;
    cJSON_ArrayForEach(u, urls) {
        url_rule_t *r = &d->rules[d->rule_count++];
        if (parse_rule(u, r, d->name, err, err_size) != 0)
            return -1;
    }
    return 0;
}

site_config_t *site_config_parse(const char *json, char *err, size_t err_size)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) {
        set_err(err, err_size, "JSON inválido");
        return NULL;
    }
    site_config_t *cfg = calloc(1, sizeof(*cfg));
    if (!cfg) {
        cJSON_Delete(root);
        return NULL;
    }

    const cJSON *domains = cJSON_GetObjectItemCaseSensitive(root, "domains");
    if (!cJSON_IsArray(domains)) {
        set_err(err, err_size, "falta el arreglo 'domains'");
        goto fail;
    }
    int n = cJSON_GetArraySize(domains);
    cfg->domains = calloc((size_t)(n > 0 ? n : 1), sizeof(*cfg->domains));
    if (!cfg->domains)
        goto fail;
    const cJSON *d;
    cJSON_ArrayForEach(d, domains) {
        domain_rule_t *dr = &cfg->domains[cfg->domain_count++];
        if (parse_domain(d, dr, err, err_size) != 0)
            goto fail;
    }
    cJSON_Delete(root);
    return cfg;

fail:
    cJSON_Delete(root);
    site_config_free(cfg);
    return NULL;
}

const domain_rule_t *site_config_find_domain(const site_config_t *cfg, const char *host)
{
    const domain_rule_t *best = NULL;
    int best_score = -1;
    for (size_t i = 0; cfg && i < cfg->domain_count; i++) {
        const domain_rule_t *d = &cfg->domains[i];
        if (!wildcard_match_nocase(d->name, host))
            continue;
        int score = wildcard_specificity(d->name);
        if (score > best_score) {
            best = d;
            best_score = score;
        }
    }
    return best;
}

const url_rule_t *domain_find_rule(const domain_rule_t *domain, const char *path)
{
    const url_rule_t *best = NULL;
    int best_score = -1;
    for (size_t i = 0; i < domain->rule_count; i++) {
        const url_rule_t *r = &domain->rules[i];
        if (!wildcard_match(r->pattern, path))
            continue;
        int score = wildcard_specificity(r->pattern);
        if (score > best_score) {
            best = r;
            best_score = score;
        }
    }
    return best;
}

long url_rule_ttl_for_path(const url_rule_t *rule, const char *path)
{
    char ext[16];
    zc_path_extension(path, ext, sizeof(ext));
    if (!ext[0])
        zc_strlcpy(ext, "html", sizeof(ext));

    long wildcard_ttl = -1;
    for (size_t i = 0; i < rule->file_type_count; i++) {
        const file_type_rule_t *ft = &rule->file_types[i];
        if (strcmp(ft->extension, ext) == 0)
            return ft->ttl > 0 ? ft->ttl : -1;
        if (strcmp(ft->extension, "*") == 0)
            wildcard_ttl = ft->ttl > 0 ? ft->ttl : -1;
    }
    return wildcard_ttl;
}
