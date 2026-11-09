/*
 * auth.h - Autenticación de peticiones en la Zonal Cache.
 *
 * API Key (x-api-key):
 *   La configuración trae el SHA-256 (hexadecimal) de cada clave válida; la caché
 *   calcula el SHA-256 de la clave recibida y lo compara en tiempo constante.
 *   Así la clave en texto plano nunca viaja en la configuración.
 *
 * Usuario y contraseña:
 *   La persona usuaria se autentica en el formulario de la UI. El API Node.js emite
 *   un JWT HS256 firmado con el secreto compartido ZC_SESSION_SECRET con los claims
 *   { "domain": "<dominio>", "sub": "<usuario>", "rule": "<id de la regla>", "exp": <unix> }.
 *   La caché verifica firma, expiración, dominio y regla en cada petición (cookie
 *   zc_session o Authorization: Bearer). El claim "rule" es opcional: si falta, la
 *   sesión sirve para todas las URLs con usuario y contraseña del dominio.
 */
#ifndef ZC_AUTH_H
#define ZC_AUTH_H

#include <stddef.h>
#include <time.h>

#define AUTH_SESSION_COOKIE "zc_session"

/* SHA-256 en hexadecimal (out: 65 bytes). */
void auth_sha256_hex(const char *data, size_t len, char out[65]);

/* Comparación en tiempo constante. Devuelve 1 si son iguales. */
int auth_secure_equals(const char *a, const char *b);

/* Devuelve 1 si el SHA-256 de key está en hashes. */
int auth_api_key_valid(const char *key, char *const *hashes, size_t hash_count);

typedef enum {
    TOKEN_OK = 0,
    TOKEN_MALFORMED,
    TOKEN_BAD_SIGNATURE,
    TOKEN_EXPIRED,
    TOKEN_WRONG_DOMAIN,
    TOKEN_WRONG_RULE
} token_status_t;

/*
 * Verifica un JWT HS256. Si es válido para domain (sin distinguir mayúsculas),
 * copia el usuario (claim "sub") en user y su expiración en exp_out (puede ser NULL).
 */
token_status_t auth_token_verify(const char *secret, const char *token, const char *domain,
                                 time_t now, char *user, size_t user_size, time_t *exp_out);

/*
 * Crea un JWT HS256 con los claims domain, sub y exp. Lo usa la batería de pruebas
 * y sirve como referencia del formato que debe emitir el API Node.js.
 * Devuelve memoria nueva o NULL.
 */
char *auth_token_create(const char *secret, const char *domain, const char *user, time_t exp);

/*
 * Igual que auth_token_verify, pero si rule_id no es NULL y el token trae el claim
 * "rule", ese claim debe ser rule_id (si no, TOKEN_WRONG_RULE).
 */
token_status_t auth_token_verify_scope(const char *secret, const char *token, const char *domain,
                                       const char *rule_id, time_t now, char *user,
                                       size_t user_size, time_t *exp_out);

/* Igual que auth_token_create, con el claim "rule" si rule_id no es NULL. */
char *auth_token_create_scoped(const char *secret, const char *domain, const char *user,
                               const char *rule_id, time_t exp);

const char *auth_token_status_text(token_status_t status);

#endif
