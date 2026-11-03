/*
 * settings.h - Configuración del proceso leída de variables de entorno.
 *
 * Ningún valor de despliegue está quemado en el código: todo se inyecta por
 * variables de entorno (ver zonal-cache/README.md, sección "Variables de entorno").
 */
#ifndef ZC_SETTINGS_H
#define ZC_SETTINGS_H

#include <stddef.h>

typedef struct {
    char zone[64];              /* ZC_ZONE */
    int http_port;              /* ZC_HTTP_PORT */
    int https_port;             /* ZC_HTTPS_PORT (0 = deshabilitado) */
    char tls_cert_file[512];    /* ZC_TLS_CERT_FILE */
    char tls_key_file[512];     /* ZC_TLS_KEY_FILE */
    char cache_dir[512];        /* ZC_CACHE_DIR */
    char config_url[1024];      /* ZC_CONFIG_URL */
    char config_token[1024];    /* ZC_CONFIG_TOKEN */
    char config_file[512];      /* ZC_CONFIG_FILE */
    int config_refresh_seconds; /* ZC_CONFIG_REFRESH_SECONDS */
    char login_url[1024];       /* ZC_LOGIN_URL */
    char session_secret[256];   /* ZC_SESSION_SECRET */
    int origin_timeout_seconds; /* ZC_ORIGIN_TIMEOUT_SECONDS */
    int origin_verify_tls;      /* ZC_ORIGIN_VERIFY_TLS */
    size_t max_body_bytes;      /* ZC_MAX_BODY_BYTES */
    char log_level[16];         /* ZC_LOG_LEVEL */
} settings_t;

/*
 * Lee las variables de entorno. Devuelve 0 si la configuración es utilizable o -1
 * y un mensaje en err si falta algo obligatorio o hay valores inválidos.
 */
int settings_load(settings_t *s, char *err, size_t err_size);

#endif
