#include <stdlib.h>

#include "../src/config.h"
#include "test.h"

static const char *VARS[] = {"DI_PORT",         "DI_DOMAINS",    "DI_ZONE_IPS",
                             "DI_DEFAULT_ZONE", "DI_UPSTREAM_DNS", "DI_TTL",
                             "DI_UPSTREAM_PORT", "DI_UPSTREAM_TIMEOUT_MS"};

static void clear_env(void) {
    for (size_t i = 0; i < sizeof(VARS) / sizeof(VARS[0]); i++) unsetenv(VARS[i]);
}

static void test_defaults_and_values(void) {
    config_t cfg;
    char err[256];
    uint32_t ip;

    clear_env();
    setenv("DI_ZONE_IPS", "america=10.96.0.10,europa=10.96.0.11", 1);
    CHECK(config_from_env(&cfg, err, sizeof(err)) == 0);
    CHECK(cfg.port == 5353);
    CHECK(cfg.ttl == 30);
    CHECK_STR(cfg.upstream_host, "8.8.8.8");
    CHECK_STR(cfg.default_zone, "america");
    CHECK(cfg.n_zones == 2 && cfg.n_domains == 0);
    CHECK(config_zone_ip(&cfg, "europa", &ip) == 0 && ip == ((10u << 24) | (96u << 16) | 11u));
    CHECK(config_zone_ip(&cfg, "asia", &ip) == -1);

    setenv("DI_PORT", "5300", 1);
    setenv("DI_TTL", "60", 1);
    setenv("DI_DEFAULT_ZONE", "EUROPA", 1); /* se normaliza a minúsculas */
    setenv("DI_DOMAINS", "Apache.P2.local, *.cdn.p2.local", 1);
    CHECK(config_from_env(&cfg, err, sizeof(err)) == 0);
    CHECK(cfg.port == 5300 && cfg.ttl == 60);
    CHECK_STR(cfg.default_zone, "europa");
    CHECK(cfg.n_domains == 2);
    CHECK_STR(cfg.domains[0], "apache.p2.local");
    clear_env();
}

static void test_invalid_values(void) {
    config_t cfg;
    char err[256];

    clear_env();
    CHECK(config_from_env(&cfg, err, sizeof(err)) == -1); /* falta DI_ZONE_IPS */

    setenv("DI_ZONE_IPS", "america=10.0.0.1", 1);
    setenv("DI_PORT", "99999", 1);
    CHECK(config_from_env(&cfg, err, sizeof(err)) == -1);
    unsetenv("DI_PORT");

    setenv("DI_ZONE_IPS", "america=999.0.0.1", 1);
    CHECK(config_from_env(&cfg, err, sizeof(err)) == -1);

    setenv("DI_ZONE_IPS", "america", 1);
    CHECK(config_from_env(&cfg, err, sizeof(err)) == -1);

    setenv("DI_ZONE_IPS", "america=10.0.0.1", 1);
    setenv("DI_DEFAULT_ZONE", "asia", 1); /* la zona por defecto debe existir */
    CHECK(config_from_env(&cfg, err, sizeof(err)) == -1);
    clear_env();
}

static void test_domain_match(void) {
    config_t cfg;
    char err[256];

    clear_env();
    setenv("DI_ZONE_IPS", "america=10.0.0.1", 1);
    setenv("DI_DOMAINS", "apache.p2.local,*.cdn.p2.local", 1);
    CHECK(config_from_env(&cfg, err, sizeof(err)) == 0);

    CHECK(config_domain_match(&cfg, "apache.p2.local") == 1);
    CHECK(config_domain_match(&cfg, "img.cdn.p2.local") == 1);
    CHECK(config_domain_match(&cfg, "a.b.cdn.p2.local") == 1);
    CHECK(config_domain_match(&cfg, "cdn.p2.local") == 0);        /* el comodín exige un subdominio */
    CHECK(config_domain_match(&cfg, "www.apache.p2.local") == 0); /* sin comodín es exacto */
    CHECK(config_domain_match(&cfg, "google.com") == 0);
    CHECK(config_domain_match(&cfg, "xcdn.p2.local") == 0);
    clear_env();
}

void run_config_tests(void) {
    test_defaults_and_values();
    test_invalid_values();
    test_domain_match();
}
