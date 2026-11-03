/*
 * settings.c - Lectura y validación de variables de entorno.
 */
#include "settings.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void read_str(const char *name, char *out, size_t size, const char *def)
{
    const char *v = getenv(name);
    zc_strlcpy(out, (v && *v) ? v : def, size);
}

/* Lee un entero en [min, max]. Devuelve -1 si el valor no es válido. */
static int read_long(const char *name, long def, long min, long max, long *out)
{
    const char *v = getenv(name);
    if (!v || !*v) {
        *out = def;
        return 0;
    }
    char *endp = NULL;
    long n = strtol(v, &endp, 10);
    if (*endp != '\0' || n < min || n > max)
        return -1;
    *out = n;
    return 0;
}

int settings_load(settings_t *s, char *err, size_t err_size)
{
    memset(s, 0, sizeof(*s));
    read_str("ZC_ZONE", s->zone, sizeof(s->zone), "default");
    read_str("ZC_TLS_CERT_FILE", s->tls_cert_file, sizeof(s->tls_cert_file), "");
    read_str("ZC_TLS_KEY_FILE", s->tls_key_file, sizeof(s->tls_key_file), "");
    read_str("ZC_CACHE_DIR", s->cache_dir, sizeof(s->cache_dir), "/var/cache/zonal-cache");
    read_str("ZC_CONFIG_URL", s->config_url, sizeof(s->config_url), "");
    read_str("ZC_CONFIG_TOKEN", s->config_token, sizeof(s->config_token), "");
    read_str("ZC_CONFIG_FILE", s->config_file, sizeof(s->config_file), "");
    read_str("ZC_LOGIN_URL", s->login_url, sizeof(s->login_url), "");
    read_str("ZC_SESSION_SECRET", s->session_secret, sizeof(s->session_secret), "");
    read_str("ZC_LOG_LEVEL", s->log_level, sizeof(s->log_level), "info");

    long http_port, https_port, refresh, timeout, verify, max_body;
    const char *bad = NULL;
    if (read_long("ZC_HTTP_PORT", 8080, 1, 65535, &http_port) != 0)
        bad = "ZC_HTTP_PORT";
    else if (read_long("ZC_HTTPS_PORT", 0, 0, 65535, &https_port) != 0)
        bad = "ZC_HTTPS_PORT";
    else if (read_long("ZC_CONFIG_REFRESH_SECONDS", 30, 1, 86400, &refresh) != 0)
        bad = "ZC_CONFIG_REFRESH_SECONDS";
    else if (read_long("ZC_ORIGIN_TIMEOUT_SECONDS", 15, 1, 600, &timeout) != 0)
        bad = "ZC_ORIGIN_TIMEOUT_SECONDS";
    else if (read_long("ZC_ORIGIN_VERIFY_TLS", 1, 0, 1, &verify) != 0)
        bad = "ZC_ORIGIN_VERIFY_TLS";
    else if (read_long("ZC_MAX_BODY_BYTES", 10L * 1024 * 1024, 1024, 1024L * 1024 * 1024,
                       &max_body) != 0)
        bad = "ZC_MAX_BODY_BYTES";
    if (bad) {
        snprintf(err, err_size, "valor inválido en %s", bad);
        return -1;
    }
    s->http_port = (int)http_port;
    s->https_port = (int)https_port;
    s->config_refresh_seconds = (int)refresh;
    s->origin_timeout_seconds = (int)timeout;
    s->origin_verify_tls = (int)verify;
    s->max_body_bytes = (size_t)max_body;

    if (!s->config_url[0] && !s->config_file[0]) {
        snprintf(err, err_size, "se requiere ZC_CONFIG_URL o ZC_CONFIG_FILE");
        return -1;
    }
    if (s->https_port && (!s->tls_cert_file[0] || !s->tls_key_file[0])) {
        snprintf(err, err_size, "ZC_HTTPS_PORT requiere ZC_TLS_CERT_FILE y ZC_TLS_KEY_FILE");
        return -1;
    }
    if (s->https_port == s->http_port) {
        snprintf(err, err_size, "ZC_HTTP_PORT y ZC_HTTPS_PORT deben ser distintos");
        return -1;
    }
    return 0;
}
