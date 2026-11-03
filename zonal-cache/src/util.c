/*
 * util.c - Implementación de las funciones auxiliares de util.h.
 */
#include "util.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

size_t zc_strlcpy(char *dst, const char *src, size_t size)
{
    size_t len = strlen(src);
    if (size > 0) {
        size_t n = len < size - 1 ? len : size - 1;
        memcpy(dst, src, n);
        dst[n] = '\0';
    }
    return len;
}

int zc_strcaseeq(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return 0;
        a++;
        b++;
    }
    return *a == *b;
}

char *zc_strndup(const char *s, size_t n)
{
    char *out = malloc(n + 1);
    if (!out)
        return NULL;
    memcpy(out, s, n);
    out[n] = '\0';
    return out;
}

void zc_str_tolower(char *s)
{
    for (; *s; s++)
        *s = (char)tolower((unsigned char)*s);
}

char *zc_str_trim(char *s)
{
    while (*s && isspace((unsigned char)*s))
        s++;
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1]))
        s[--len] = '\0';
    return s;
}

void zc_path_extension(const char *path, char *out, size_t out_size)
{
    out[0] = '\0';
    size_t end = strcspn(path, "?#");
    size_t start = end;
    while (start > 0 && path[start - 1] != '/')
        start--;

    const char *dot = NULL;
    for (size_t i = start; i < end; i++)
        if (path[i] == '.')
            dot = path + i;
    if (!dot || dot + 1 >= path + end)
        return;

    size_t n = (size_t)(path + end - (dot + 1));
    if (n >= out_size)
        n = out_size - 1;
    memcpy(out, dot + 1, n);
    out[n] = '\0';
    zc_str_tolower(out);
}

uint64_t zc_fnv1a64(const char *data, size_t len)
{
    uint64_t hash = 1469598103934665603ULL;
    for (size_t i = 0; i < len; i++) {
        hash ^= (unsigned char)data[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

void zc_hex_encode(const unsigned char *in, size_t len, char *out)
{
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        out[2 * i] = digits[in[i] >> 4];
        out[2 * i + 1] = digits[in[i] & 0x0f];
    }
    out[2 * len] = '\0';
}

static const char B64URL[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

size_t zc_base64url_encode(const unsigned char *in, size_t len, char *out)
{
    size_t o = 0;
    size_t i = 0;
    for (; i + 2 < len; i += 3) {
        uint32_t v = ((uint32_t)in[i] << 16) | ((uint32_t)in[i + 1] << 8) | in[i + 2];
        out[o++] = B64URL[(v >> 18) & 63];
        out[o++] = B64URL[(v >> 12) & 63];
        out[o++] = B64URL[(v >> 6) & 63];
        out[o++] = B64URL[v & 63];
    }
    if (len - i == 1) {
        uint32_t v = (uint32_t)in[i] << 16;
        out[o++] = B64URL[(v >> 18) & 63];
        out[o++] = B64URL[(v >> 12) & 63];
    } else if (len - i == 2) {
        uint32_t v = ((uint32_t)in[i] << 16) | ((uint32_t)in[i + 1] << 8);
        out[o++] = B64URL[(v >> 18) & 63];
        out[o++] = B64URL[(v >> 12) & 63];
        out[o++] = B64URL[(v >> 6) & 63];
    }
    out[o] = '\0';
    return o;
}

static int b64url_value(char c)
{
    if (c >= 'A' && c <= 'Z')
        return c - 'A';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 26;
    if (c >= '0' && c <= '9')
        return c - '0' + 52;
    if (c == '-')
        return 62;
    if (c == '_')
        return 63;
    return -1;
}

long zc_base64url_decode(const char *in, unsigned char *out, size_t out_size)
{
    size_t len = strlen(in);
    if (len % 4 == 1)
        return -1;
    size_t o = 0;
    uint32_t acc = 0;
    int bits = 0;
    for (size_t i = 0; i < len; i++) {
        int v = b64url_value(in[i]);
        if (v < 0)
            return -1;
        acc = (acc << 6) | (uint32_t)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (o >= out_size)
                return -1;
            out[o++] = (unsigned char)((acc >> bits) & 0xff);
        }
    }
    return (long)o;
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

void zc_url_decode(char *s)
{
    char *w = s;
    for (char *r = s; *r; r++) {
        if (*r == '%' && hex_value(r[1]) >= 0 && hex_value(r[2]) >= 0) {
            *w++ = (char)(hex_value(r[1]) * 16 + hex_value(r[2]));
            r += 2;
        } else if (*r == '+') {
            *w++ = ' ';
        } else {
            *w++ = *r;
        }
    }
    *w = '\0';
}

char *zc_url_encode(const char *s)
{
    size_t len = strlen(s);
    char *out = malloc(len * 3 + 1);
    if (!out)
        return NULL;
    char *w = out;
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            *w++ = (char)c;
        } else {
            sprintf(w, "%%%02X", c);
            w += 3;
        }
    }
    *w = '\0';
    return out;
}

int zc_query_param(const char *query, const char *name, char *out, size_t out_size)
{
    if (!query)
        return 0;
    size_t name_len = strlen(name);
    const char *p = query;
    while (*p) {
        const char *end = strchr(p, '&');
        size_t seg_len = end ? (size_t)(end - p) : strlen(p);
        if (seg_len > name_len && strncmp(p, name, name_len) == 0 && p[name_len] == '=') {
            size_t vlen = seg_len - name_len - 1;
            if (vlen >= out_size)
                vlen = out_size - 1;
            memcpy(out, p + name_len + 1, vlen);
            out[vlen] = '\0';
            zc_url_decode(out);
            return 1;
        }
        if (!end)
            break;
        p = end + 1;
    }
    return 0;
}
