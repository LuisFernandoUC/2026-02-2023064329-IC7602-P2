/*
 * test.h - Mini framework de pruebas unitarias (sin dependencias externas).
 *
 *   TEST(nombre) { CHECK(cond); CHECK_EQ_INT(a, b); CHECK_EQ_STR(a, b); }
 *   RUN(nombre);   desde la función de la suite
 */
#ifndef ZC_TEST_H
#define ZC_TEST_H

#include <stdio.h>
#include <string.h>

extern int test_current_failed;
extern int tests_run;
extern int tests_failed;

#define TEST(name) static void name(void)

#define CHECK(cond)                                                                    \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            fprintf(stderr, "    FALLO %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
            test_current_failed = 1;                                                   \
        }                                                                              \
    } while (0)

#define CHECK_EQ_INT(actual, expected)                                                 \
    do {                                                                               \
        long long a_ = (long long)(actual), e_ = (long long)(expected);                \
        if (a_ != e_) {                                                                \
            fprintf(stderr, "    FALLO %s:%d: %s = %lld, se esperaba %lld\n", __FILE__, \
                    __LINE__, #actual, a_, e_);                                        \
            test_current_failed = 1;                                                   \
        }                                                                              \
    } while (0)

#define CHECK_EQ_STR(actual, expected)                                                 \
    do {                                                                               \
        const char *a_ = (actual), *e_ = (expected);                                   \
        if (!a_ || !e_ || strcmp(a_, e_) != 0) {                                       \
            fprintf(stderr, "    FALLO %s:%d: %s = \"%s\", se esperaba \"%s\"\n",       \
                    __FILE__, __LINE__, #actual, a_ ? a_ : "(null)", e_ ? e_ : "(null)"); \
            test_current_failed = 1;                                                   \
        }                                                                              \
    } while (0)

#define RUN(name)                                                                      \
    do {                                                                               \
        test_current_failed = 0;                                                       \
        name();                                                                        \
        tests_run++;                                                                   \
        if (test_current_failed) {                                                     \
            tests_failed++;                                                            \
            fprintf(stderr, "  [FALLÓ] %s\n", #name);                                  \
        } else {                                                                       \
            printf("  [ok] %s\n", #name);                                              \
        }                                                                              \
    } while (0)

/* Suites (una por módulo). */
void suite_util(void);
void suite_policy(void);
void suite_wildcard(void);
void suite_http(void);
void suite_site_config(void);
void suite_cache(void);
void suite_auth(void);
void suite_settings(void);
void suite_config_store(void);
void suite_proxy(void);
void suite_log(void);

/* Crea un directorio temporal único para pruebas de disco. */
void test_tmpdir(char *out, size_t size);

#endif
