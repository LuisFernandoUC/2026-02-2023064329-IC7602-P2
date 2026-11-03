/*
 * wildcard.c - Coincidencia iterativa con retroceso sobre el último '*'.
 */
#include "wildcard.h"

#include <ctype.h>

static int match(const char *pattern, const char *text, int nocase)
{
    const char *star = NULL;  /* posición del último '*' visto en el patrón */
    const char *resume = NULL; /* posición del texto donde reintentar tras ese '*' */

    while (*text) {
        char pc = nocase ? (char)tolower((unsigned char)*pattern) : *pattern;
        char tc = nocase ? (char)tolower((unsigned char)*text) : *text;

        if (*pattern == '*') {
            star = pattern++;
            resume = text;
        } else if (*pattern == '?' || (*pattern && pc == tc)) {
            pattern++;
            text++;
        } else if (star) {
            /* El '*' absorbe un carácter más y se reintenta. */
            pattern = star + 1;
            text = ++resume;
        } else {
            return 0;
        }
    }
    while (*pattern == '*')
        pattern++;
    return *pattern == '\0';
}

int wildcard_match(const char *pattern, const char *text)
{
    return match(pattern, text, 0);
}

int wildcard_match_nocase(const char *pattern, const char *text)
{
    return match(pattern, text, 1);
}

int wildcard_specificity(const char *pattern)
{
    int n = 0;
    for (; *pattern; pattern++)
        if (*pattern != '*' && *pattern != '?')
            n++;
    return n;
}
