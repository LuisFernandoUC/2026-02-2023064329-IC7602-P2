/*
 * origin.h - Cliente HTTP/HTTPS hacia el servidor de origen (libcurl).
 */
#ifndef ZC_ORIGIN_H
#define ZC_ORIGIN_H

#include <stddef.h>

#include "proxy.h"

typedef struct {
    long timeout_seconds;
    int verify_tls;   /* 0 permite certificados autofirmados en el origen */
    size_t max_body;  /* tamaño máximo de respuesta aceptado */
} origin_options_t;

/* Implementación de origin_fetch_fn; ctx debe ser un origin_options_t*. */
int origin_fetch_curl(void *ctx, const origin_request_t *req, http_response_t *resp);

/*
 * GET simple usado para descargar la configuración. bearer puede ser NULL.
 * Devuelve el cuerpo (memoria nueva, terminado en '\0') si el status es 200, o NULL.
 */
char *origin_get_text(const char *url, const char *bearer, long timeout_seconds,
                      char *err, size_t err_size);

#endif
