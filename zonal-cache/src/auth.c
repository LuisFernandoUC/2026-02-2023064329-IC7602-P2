/*
 * auth.c - SHA-256 / HMAC-SHA256 con OpenSSL y verificación de JWT HS256.
 */
#include "auth.h"
#include "util.h"

#include <cjson/cJSON.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void auth_sha256_hex(const char *data, size_t len, char out[65])
{
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256((const unsigned char *)data, len, digest);
    zc_hex_encode(digest, sizeof(digest), out);
}

int auth_secure_equals(const char *a, const char *b)
{
    size_t la = strlen(a);
    size_t lb = strlen(b);
    unsigned char diff = (unsigned char)(la != lb);
    size_t n = la < lb ? la : lb;
    for (size_t i = 0; i < n; i++)
        diff |= (unsigned char)(a[i] ^ b[i]);
    return diff == 0;
}

int auth_api_key_valid(const char *key, char *const *hashes, size_t hash_count)
{
    if (!key || !*key)
        return 0;
    char hex[65];
    auth_sha256_hex(key, strlen(key), hex);
    int valid = 0;
    for (size_t i = 0; i < hash_count; i++)
        valid |= auth_secure_equals(hex, hashes[i]);
    return valid;
}

static void hmac_sha256_b64(const char *secret, const char *data, size_t len, char *out)
{
    unsigned char mac[EVP_MAX_MD_SIZE];
    unsigned int mac_len = 0;
    HMAC(EVP_sha256(), secret, (int)strlen(secret), (const unsigned char *)data, len, mac,
         &mac_len);
    zc_base64url_encode(mac, mac_len, out);
}

static cJSON *decode_json_part(const char *part, size_t len)
{
    char *enc = zc_strndup(part, len);
    if (!enc)
        return NULL;
    size_t cap = len + 1;
    unsigned char *raw = malloc(cap);
    long n = raw ? zc_base64url_decode(enc, raw, cap - 1) : -1;
    cJSON *json = NULL;
    if (n >= 0) {
        raw[n] = '\0';
        json = cJSON_Parse((const char *)raw);
    }
    free(enc);
    free(raw);
    return json;
}

token_status_t auth_token_verify(const char *secret, const char *token, const char *domain,
                                 time_t now, char *user, size_t user_size, time_t *exp_out)
{
    if (!secret || !*secret || !token)
        return TOKEN_MALFORMED;
    const char *dot1 = strchr(token, '.');
    const char *dot2 = dot1 ? strchr(dot1 + 1, '.') : NULL;
    if (!dot1 || !dot2 || strchr(dot2 + 1, '.'))
        return TOKEN_MALFORMED;

    /* Encabezado: solo se acepta HS256 (evita el ataque "alg": "none"). */
    cJSON *header = decode_json_part(token, (size_t)(dot1 - token));
    const cJSON *alg = header ? cJSON_GetObjectItemCaseSensitive(header, "alg") : NULL;
    int alg_ok = cJSON_IsString(alg) && strcmp(alg->valuestring, "HS256") == 0;
    cJSON_Delete(header);
    if (!alg_ok)
        return TOKEN_MALFORMED;

    char expected[64];
    hmac_sha256_b64(secret, token, (size_t)(dot2 - token), expected);
    if (!auth_secure_equals(expected, dot2 + 1))
        return TOKEN_BAD_SIGNATURE;

    cJSON *payload = decode_json_part(dot1 + 1, (size_t)(dot2 - dot1 - 1));
    if (!payload)
        return TOKEN_MALFORMED;
    token_status_t status = TOKEN_OK;
    const cJSON *exp = cJSON_GetObjectItemCaseSensitive(payload, "exp");
    const cJSON *dom = cJSON_GetObjectItemCaseSensitive(payload, "domain");
    const cJSON *sub = cJSON_GetObjectItemCaseSensitive(payload, "sub");
    if (!cJSON_IsNumber(exp) || !cJSON_IsString(dom) || !cJSON_IsString(sub))
        status = TOKEN_MALFORMED;
    else if ((time_t)exp->valuedouble <= now)
        status = TOKEN_EXPIRED;
    else if (!zc_strcaseeq(dom->valuestring, domain))
        status = TOKEN_WRONG_DOMAIN;

    if (status == TOKEN_OK) {
        if (user)
            zc_strlcpy(user, sub->valuestring, user_size);
        if (exp_out)
            *exp_out = (time_t)exp->valuedouble;
    }
    cJSON_Delete(payload);
    return status;
}

char *auth_token_create(const char *secret, const char *domain, const char *user, time_t exp)
{
    static const char header[] = "{\"alg\":\"HS256\",\"typ\":\"JWT\"}";

    cJSON *payload = cJSON_CreateObject();
    if (!payload)
        return NULL;
    cJSON_AddStringToObject(payload, "domain", domain);
    cJSON_AddStringToObject(payload, "sub", user);
    cJSON_AddNumberToObject(payload, "exp", (double)exp);
    char *payload_str = cJSON_PrintUnformatted(payload);
    cJSON_Delete(payload);
    if (!payload_str)
        return NULL;

    size_t plen = strlen(payload_str);
    size_t cap = (sizeof(header) + plen) * 2 + 128;
    char *out = malloc(cap);
    if (!out) {
        free(payload_str);
        return NULL;
    }
    size_t o = zc_base64url_encode((const unsigned char *)header, sizeof(header) - 1, out);
    out[o++] = '.';
    o += zc_base64url_encode((const unsigned char *)payload_str, plen, out + o);
    free(payload_str);

    char sig[64];
    hmac_sha256_b64(secret, out, o, sig);
    out[o++] = '.';
    zc_strlcpy(out + o, sig, cap - o);
    return out;
}

const char *auth_token_status_text(token_status_t status)
{
    switch (status) {
    case TOKEN_OK:
        return "ok";
    case TOKEN_MALFORMED:
        return "token mal formado";
    case TOKEN_BAD_SIGNATURE:
        return "firma inválida";
    case TOKEN_EXPIRED:
        return "sesión expirada";
    case TOKEN_WRONG_DOMAIN:
        return "el token pertenece a otro dominio";
    }
    return "desconocido";
}
