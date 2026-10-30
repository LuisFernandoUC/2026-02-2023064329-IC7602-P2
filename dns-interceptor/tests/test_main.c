#include "test.h"

int tests_run = 0;
int tests_failed = 0;

int main(void) {
    run_dns_tests();
    run_geo_tests();
    run_config_tests();
    run_resolver_tests();

    printf("%d verificaciones, %d fallidas\n", tests_run, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
