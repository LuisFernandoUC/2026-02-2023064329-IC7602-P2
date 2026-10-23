/*
 * http.c - Implementación del parser HTTP y de la construcción de respuestas.
 */
#include "http.h"
#include "util.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *find_head_end(const char *buf, size_t len)
{
    for (size_t i = 0; i + 3 < len; i++)
        if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' && buf[i + 3] == '\n')
            return buf + i;
    return NULL;
}

static int is_token_char(char c)
{
    return isalnum((unsigned char)c) || strchr("!#$%&'*+-.^_`|~", c) != NULL;
}

long http_parse_request_head(const char *buf, size_t len, http_request_t *req)
{
    memset(req, 0, sizeof(*req));
    const char *end = find_head_end(buf, len);
    if (!end)
        return 0;
    size_t head_len = (size_t)(end - buf) + 4;

    req->head = zc_strndup(buf, head_len - 4);
    if (!req->head)
        return -1;

    /* Línea de petición: METHOD SP TARGET SP VERSION */
    char *line_end = strstr(req->head, "\r\n");
    char *lines = NULL;
    if (line_end) {
        *line_end = '\0';
        lines = line_end + 2;
    }
    char *save = NULL;
    req->method = strtok_r(req->head, " ", &save);
    req->target = strtok_r(NULL, " ", &save);
    req->version = strtok_r(NULL, " ", &save);
    if (!req->method || !req->target || !req->version || strtok_r(NULL, " ", &save))
        goto malformed;
    for (const char *m = req->method; *m; m++)
        if (!is_token_char(*m))
            goto malformed;
    if (strncmp(req->version, "HTTP/1.", 7) != 0 || req->target[0] != '/')
        goto malformed;

    const char *q = strchr(req->target, '?');
    if (q) {
        req->path = zc_strndup(req->target, (size_t)(q - req->target));
        req->query = zc_strndup(q + 1, strlen(q + 1));
    } else {
        req->path = zc_strndup(req->target, strlen(req->target));
    }
    if (!req->path)
        goto malformed;

    /* Encabezados: Name: value */
    while (lines && *lines) {
        char *next = strstr(lines, "\r\n");
        if (next) {
            *next = '\0';
            next += 2;
        }
        char *colon = strchr(lines, ':');
        if (!colon || colon == lines || req->header_count >= HTTP_MAX_HEADERS)
            goto malformed;
        *colon = '\0';
        for (const char *n = lines; *n; n++)
            if (!is_token_char(*n))
                goto malformed;
        req->headers[req->header_count].name = lines;
        req->headers[req->header_count].value = zc_str_trim(colon + 1);
        req->header_count++;
        lines = next;
    }
    return (long)head_len;

malformed:
    http_request_free(req);
    return -1;
}

int http_request_set_body(http_request_t *req, const unsigned char *body, size_t len)
{
    free(req->body);
    req->body = NULL;
    req->body_len = 0;
    if (len == 0)
        return 0;
    req->body = malloc(len);
    if (!req->body)
        return -1;
    memcpy(req->body, body, len);
    req->body_len = len;
    return 0;
}

void http_request_free(http_request_t *req)
{
    free(req->head);
    free(req->path);
    free(req->query);
    free(req->body);
    memset(req, 0, sizeof(*req));
}

const char *http_request_header(const http_request_t *req, const char *name)
{
    for (size_t i = 0; i < req->header_count; i++)
        if (zc_strcaseeq(req->headers[i].name, name))
            return req->headers[i].value;
    return NULL;
}

long http_request_content_length(const http_request_t *req)
{
    const char *v = http_request_header(req, "Content-Length");
    if (!v)
        return 0;
    char *endp = NULL;
    long n = strtol(v, &endp, 10);
    if (endp == v || *endp != '\0' || n < 0)
        return -1;
    return n;
}

int http_request_host(const http_request_t *req, char *out, size_t out_size)
{
    const char *host = http_request_header(req, "Host");
    if (!host || !*host)
        return -1;
    size_t n = strcspn(host, ":");
    if (n >= out_size)
        n = out_size - 1;
    memcpy(out, host, n);
    out[n] = '\0';
    zc_str_tolower(out);
    return 0;
}

int http_request_cookie(const http_request_t *req, const char *name, char *out, size_t out_size)
{
    size_t name_len = strlen(name);
    for (size_t i = 0; i < req->header_count; i++) {
        if (!zc_strcaseeq(req->headers[i].name, "Cookie"))
            continue;
        const char *p = req->headers[i].value;
        while (*p) {
            while (*p == ' ' || *p == ';')
                p++;
            size_t seg = strcspn(p, ";");
            if (seg > name_len && strncmp(p, name, name_len) == 0 && p[name_len] == '=') {
                size_t vlen = seg - name_len - 1;
                if (vlen >= out_size)
                    vlen = out_size - 1;
                memcpy(out, p + name_len + 1, vlen);
                out[vlen] = '\0';
                return 1;
            }
            p += seg;
        }
    }
    return 0;
}

void http_response_init(http_response_t *resp, int status)
{
    memset(resp, 0, sizeof(*resp));
    resp->status = status;
}

void http_response_free(http_response_t *resp)
{
    for (size_t i = 0; i < resp->header_count; i++) {
        free(resp->headers[i].name);
        free(resp->headers[i].value);
    }
    free(resp->body);
    memset(resp, 0, sizeof(*resp));
}

int http_response_add_header(http_response_t *resp, const char *name, const char *value)
{
    if (resp->header_count >= HTTP_MAX_HEADERS)
        return -1;
    char *n = zc_strndup(name, strlen(name));
    char *v = zc_strndup(value, strlen(value));
    if (!n || !v) {
        free(n);
        free(v);
        return -1;
    }
    resp->headers[resp->header_count].name = n;
    resp->headers[resp->header_count].value = v;
    resp->header_count++;
    return 0;
}

const char *http_response_header(const http_response_t *resp, const char *name)
{
    for (size_t i = 0; i < resp->header_count; i++)
        if (zc_strcaseeq(resp->headers[i].name, name))
            return resp->headers[i].value;
    return NULL;
}

int http_response_set_body(http_response_t *resp, const void *body, size_t len)
{
    free(resp->body);
    resp->body = NULL;
    resp->body_len = 0;
    if (len == 0)
        return 0;
    resp->body = malloc(len);
    if (!resp->body)
        return -1;
    memcpy(resp->body, body, len);
    resp->body_len = len;
    return 0;
}

char *http_response_serialize(const http_response_t *resp, size_t *out_len)
{
    size_t cap = 128 + resp->body_len;
    for (size_t i = 0; i < resp->header_count; i++)
        cap += strlen(resp->headers[i].name) + strlen(resp->headers[i].value) + 4;

    char *out = malloc(cap);
    if (!out)
        return NULL;
    size_t o = (size_t)snprintf(out, cap, "HTTP/1.1 %d %s\r\n", resp->status,
                                http_status_text(resp->status));
    for (size_t i = 0; i < resp->header_count; i++) {
        /* Content-Length y Connection los define el proxy. */
        if (zc_strcaseeq(resp->headers[i].name, "Content-Length") ||
            zc_strcaseeq(resp->headers[i].name, "Connection"))
            continue;
        o += (size_t)snprintf(out + o, cap - o, "%s: %s\r\n", resp->headers[i].name,
                              resp->headers[i].value);
    }
    o += (size_t)snprintf(out + o, cap - o, "Content-Length: %zu\r\nConnection: close\r\n\r\n",
                          resp->body_len);
    if (resp->body_len)
        memcpy(out + o, resp->body, resp->body_len);
    *out_len = o + resp->body_len;
    return out;
}

void http_response_text(http_response_t *resp, int status, const char *text)
{
    http_response_free(resp);
    http_response_init(resp, status);
    http_response_add_header(resp, "Content-Type", "text/plain; charset=utf-8");
    http_response_set_body(resp, text, strlen(text));
}

void http_response_redirect(http_response_t *resp, const char *location)
{
    http_response_free(resp);
    http_response_init(resp, 302);
    http_response_add_header(resp, "Location", location);
    http_response_add_header(resp, "Cache-Control", "no-store");
}

const char *http_status_text(int status)
{
    switch (status) {
    case 200: return "OK";
    case 201: return "Created";
    case 204: return "No Content";
    case 301: return "Moved Permanently";
    case 302: return "Found";
    case 304: return "Not Modified";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 413: return "Payload Too Large";
    case 500: return "Internal Server Error";
    case 502: return "Bad Gateway";
    case 503: return "Service Unavailable";
    case 504: return "Gateway Timeout";
    case 508: return "Loop Detected";
    default: return "Unknown";
    }
}

int http_is_hop_by_hop(const char *name)
{
    static const char *hop[] = {
        "Connection", "Keep-Alive", "Proxy-Authenticate", "Proxy-Authorization",
        "TE", "Trailer", "Transfer-Encoding", "Upgrade", "Proxy-Connection",
    };
    for (size_t i = 0; i < sizeof(hop) / sizeof(hop[0]); i++)
        if (zc_strcaseeq(name, hop[i]))
            return 1;
    return 0;
}
