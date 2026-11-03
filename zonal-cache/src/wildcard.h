/*
 * wildcard.h - Coincidencia de patrones con comodines para dominios y URLs.
 *
 *   '*' coincide con cualquier secuencia de caracteres (incluso vacía y '/').
 *   '?' coincide con exactamente un carácter.
 *
 * Ejemplos: "/static/*" coincide con "/static/css/app.css";
 *           "*.example.com" coincide con "www.example.com".
 */
#ifndef ZC_WILDCARD_H
#define ZC_WILDCARD_H

/* Devuelve 1 si text coincide completamente con pattern. */
int wildcard_match(const char *pattern, const char *text);

/* Igual que wildcard_match pero sin distinguir mayúsculas (para nombres de dominio). */
int wildcard_match_nocase(const char *pattern, const char *text);

/*
 * Especificidad de un patrón: cantidad de caracteres literales (no comodines).
 * Cuando varios patrones coinciden se elige el más específico.
 */
int wildcard_specificity(const char *pattern);

#endif
