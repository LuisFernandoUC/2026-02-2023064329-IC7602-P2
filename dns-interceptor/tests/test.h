#ifndef TEST_H
#define TEST_H

#include <stdio.h>
#include <string.h>

/* Mini framework de pruebas: cada suite cuenta sus verificaciones en estos contadores. */
extern int tests_run;
extern int tests_failed;

#define CHECK(cond)                                                              \
    do {                                                                         \
        tests_run++;                                                             \
        if (!(cond)) {                                                           \
            tests_failed++;                                                      \
            fprintf(stderr, "  FALLO %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
        }                                                                        \
    } while (0)

#define CHECK_STR(actual, expected) CHECK(strcmp((actual), (expected)) == 0)

void run_dns_tests(void);
void run_geo_tests(void);
void run_config_tests(void);
void run_resolver_tests(void);

#endif
