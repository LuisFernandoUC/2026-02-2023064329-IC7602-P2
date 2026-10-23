/*
 * util.h - Funciones auxiliares de cadenas y codificación usadas por la Zonal Cache.
 */
#ifndef ZC_UTIL_H
#define ZC_UTIL_H

#include <stddef.h>
#include <stdint.h>

/* Copia segura: siempre termina en '\0'. Devuelve la longitud de src. */
size_t zc_strlcpy(char *dst, const char *src, size_t size);

/* Comparación de cadenas sin distinguir mayúsculas (ASCII). */
int zc_strcaseeq(const char *a, const char *b);

/* Duplica los primeros n bytes de s en memoria nueva terminada en '\0'. */
char *zc_strndup(const char *s, size_t n);

/* Convierte la cadena a minúsculas en el mismo buffer. */
void zc_str_tolower(char *s);

/* Elimina espacios al inicio y al final; devuelve un puntero dentro de s. */
char *zc_str_trim(char *s);

/*
 * Extrae la extensión del último segmento de una ruta ("/a/b/logo.PNG?x=1" -> "png").
 * Escribe "" si no hay extensión. out debe tener al menos out_size bytes.
 */
void zc_path_extension(const char *path, char *out, size_t out_size);

/* Hash FNV-1a de 64 bits; se usa para nombrar los archivos de la caché en disco. */
uint64_t zc_fnv1a64(const char *data, size_t len);

/* Codifica bytes en hexadecimal en minúsculas. out debe tener 2*len+1 bytes. */
void zc_hex_encode(const unsigned char *in, size_t len, char *out);

/*
 * Base64url sin relleno (RFC 4648 §5).
 * encode: out debe tener al menos 4*ceil(len/3)+1 bytes. Devuelve la longitud escrita.
 * decode: devuelve la cantidad de bytes decodificados o -1 si la entrada es inválida.
 */
size_t zc_base64url_encode(const unsigned char *in, size_t len, char *out);
long zc_base64url_decode(const char *in, unsigned char *out, size_t out_size);

/* Decodifica %XX y '+' de un componente de query string en el mismo buffer. */
void zc_url_decode(char *s);

/* Codifica un componente para usarlo dentro de un query string. Devuelve memoria nueva. */
char *zc_url_encode(const char *s);

/*
 * Busca el parámetro name dentro de un query string ("a=1&b=2") y copia su valor
 * decodificado en out. Devuelve 1 si lo encontró, 0 si no.
 */
int zc_query_param(const char *query, const char *name, char *out, size_t out_size);

#endif
