/*
 * test_main.c - Ejecuta todas las suites y devuelve 1 si alguna prueba falla.
 */
#include "test.h"

#include <stdlib.h>
#include <unistd.h>

int test_current_failed = 0;
int tests_run = 0;
int tests_failed = 0;

void test_tmpdir(char *out, size_t size)
{
    const char *base = getenv("TMPDIR");
    snprintf(out, size, "%s/zc-test-XXXXXX", base && *base ? base : "/tmp");
    if (!mkdtemp(out)) {
        perror("mkdtemp");
        exit(2);
    }
}

int main(void)
{
    struct {
        const char *name;
        void (*fn)(void);
    } suites[] = {
        {"util", suite_util},
        {"policy (algoritmos de reemplazo)", suite_policy},
        {"wildcard", suite_wildcard},
        {"http", suite_http},
        {"site_config", suite_site_config},
        {"cache (disco)", suite_cache},
        {"auth", suite_auth},
        {"settings", suite_settings},
        {"config_store", suite_config_store},
        {"proxy", suite_proxy},
    };
    for (size_t i = 0; i < sizeof(suites) / sizeof(suites[0]); i++) {
        printf("== %s\n", suites[i].name);
        suites[i].fn();
    }
    printf("\n%d pruebas, %d fallidas\n", tests_run, tests_failed);
    return tests_failed ? 1 : 0;
}
