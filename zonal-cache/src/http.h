/*
 * http.h - Parser de peticiones HTTP/1.x y construcción de respuestas.
 *
 * Solo se soporta lo necesario para un proxy/caché simple: línea de petición,
 * encabezados y cuerpo con Content-Length (no se soporta Transfer-Encoding: chunked
 * en las peticiones del cliente). Cada conexión atiende una sola petición
 * (Connection: close).
 */
#ifndef ZC_HTTP_H
#define ZC_HTTP_H

#include <stddef.h>

#define HTTP_MAX_HEADERS 100

typedef struct {
    char *name;
    char *value;
} http_header_t;

typedef struct {
    char *head;     /* copia propia de la cabecera; los campos apuntan aquí */
    char *method;   /* "GET" */
    char *target;   /* "/ruta?query" tal como llegó */
    char *path;     /* "/ruta" (copia propia) */
    char *query;    /* "query" o NULL (copia propia) */
    char *version;  /* "HTTP/1.1" */
    http_header_t headers[HTTP_MAX_HEADERS];
    size_t header_count;
    unsigned char *body;
    size_t body_len;
} http_request_t;

typedef struct {
    int status;
    http_header_t headers[HTTP_MAX_HEADERS];
    size_t header_count;
    unsigned char *body;
    size_t body_len;
} http_response_t;

/*
 * Analiza la cabecera de una petición contenida en buf.
 * Devuelve la longitud de la cabecera (incluido el "\r\n\r\n") si está completa,
 * 0 si faltan datos y -1 si la petición está mal formada.
 */
long http_parse_request_head(const char *buf, size_t len, http_request_t *req);

/* Copia el cuerpo de la petición. Devuelve 0 si tuvo éxito. */
int http_request_set_body(http_request_t *req, const unsigned char *body, size_t len);

void http_request_free(http_request_t *req);

/* Busca un encabezado sin distinguir mayúsculas. Devuelve NULL si no existe. */
const char *http_request_header(const http_request_t *req, const char *name);

/* Valor de Content-Length (0 si no existe, -1 si es inválido). */
long http_request_content_length(const http_request_t *req);

/* Host sin puerto y en minúsculas. Devuelve 0 si existe el encabezado Host. */
int http_request_host(const http_request_t *req, char *out, size_t out_size);

/* Obtiene el valor de una cookie del encabezado Cookie. Devuelve 1 si existe. */
int http_request_cookie(const http_request_t *req, const char *name, char *out, size_t out_size);

void http_response_init(http_response_t *resp, int status);
void http_response_free(http_response_t *resp);

/* Agrega (copia) un encabezado. Devuelve 0 si tuvo éxito. */
int http_response_add_header(http_response_t *resp, const char *name, const char *value);
const char *http_response_header(const http_response_t *resp, const char *name);

/* Copia el cuerpo. Devuelve 0 si tuvo éxito. */
int http_response_set_body(http_response_t *resp, const void *body, size_t len);

/*
 * Serializa la respuesta completa (línea de estado, encabezados, Content-Length,
 * Connection: close y cuerpo). Devuelve memoria nueva y su longitud en out_len.
 */
char *http_response_serialize(const http_response_t *resp, size_t *out_len);

/* Respuesta de texto plano con el código indicado (útil para errores). */
void http_response_text(http_response_t *resp, int status, const char *text);

/* Respuesta 302 hacia location. */
void http_response_redirect(http_response_t *resp, const char *location);

const char *http_status_text(int status);

/* Encabezados hop-by-hop que un proxy no debe reenviar (RFC 7230 §6.1). */
int http_is_hop_by_hop(const char *name);

#endif
