#include <stdlib.h>

#include "../src/resolver.h"
#include "test.h"

static uint32_t ip_of(const char *text) {
    uint32_t ip = 0;
    geo_parse_ipv4(text, &ip);
    return ip;
}

static void test_failover(const geo_db_t *geo, const zone_map_t *zones) {
    config_t cfg;
    char err[256];
    zone_choice_t pick;

    setenv("DI_ZONE_IPS", "america=10.0.0.1,europa=10.0.0.2,asia=10.0.0.3", 1);
    setenv("DI_DEFAULT_ZONE", "america", 1);
    config_from_env(&cfg, err, sizeof(err));
    int all_up[CFG_MAX_ZONES] = {1, 1, 1};
    int europa_down[CFG_MAX_ZONES] = {1, 0, 1}; /* orden de DI_ZONE_IPS: america, europa, asia */
    int europa_america_down[CFG_MAX_ZONES] = {0, 0, 1};
    int all_down[CFG_MAX_ZONES] = {0, 0, 0};

    /* Todo sano: no hay desvío. */
    CHECK(resolver_choose(&cfg, geo, zones, ip_of("80.0.1.1"), all_up, &pick) == 0);
    CHECK_STR(pick.zone, "europa");
    CHECK(pick.failover == 0);

    /* Cae europa: un cliente español va a la zona por defecto. */
    CHECK(resolver_choose(&cfg, geo, zones, ip_of("80.0.1.1"), europa_down, &pick) == 0);
    CHECK_STR(pick.country, "ES");
    CHECK_STR(pick.zone, "america");
    CHECK(pick.ip == ip_of("10.0.0.1"));
    CHECK(pick.failover == 1);

    /* Los clientes de zonas sanas no se ven afectados. */
    CHECK(resolver_choose(&cfg, geo, zones, ip_of("133.1.1.1"), europa_down, &pick) == 0);
    CHECK_STR(pick.zone, "asia");
    CHECK(pick.failover == 0);

    /* Caen europa y la zona por defecto: se usa la primera sana. */
    CHECK(resolver_choose(&cfg, geo, zones, ip_of("80.0.1.1"), europa_america_down, &pick) == 0);
    CHECK_STR(pick.zone, "asia");
    CHECK(pick.failover == 1);

    /* País sin zona -> zona por defecto caída -> otra zona sana. */
    CHECK(resolver_choose(&cfg, geo, zones, ip_of("250.1.1.1"), europa_america_down, &pick) == 0);
    CHECK_STR(pick.zone, "asia");
    CHECK(pick.failover == 1);

    /* Ninguna responde: se mantiene la zona del país en vez de fallar. */
    CHECK(resolver_choose(&cfg, geo, zones, ip_of("80.0.1.1"), all_down, &pick) == 0);
    CHECK_STR(pick.zone, "europa");
    CHECK(pick.failover == 0);
}

void run_resolver_tests(void) {
    config_t cfg;
    char err[256];
    zone_choice_t pick;

    unsetenv("DI_DOMAINS");
    unsetenv("DI_DEFAULT_ZONE");
    setenv("DI_ZONE_IPS", "america=10.0.0.1,europa=10.0.0.2,asia=10.0.0.3", 1);
    config_from_env(&cfg, err, sizeof(err));

    geo_db_t *geo = geo_load("tests/data/mini-country.csv");
    zone_map_t *zones = zones_load("tests/data/mini-zones.csv");
    CHECK(geo != NULL && zones != NULL);
    if (!geo || !zones) return;

    /* Cada país va a la caché de su zona. */
    CHECK(resolver_choose(&cfg, geo, zones, ip_of("8.8.8.8"), NULL, &pick) == 0);
    CHECK_STR(pick.country, "US");
    CHECK_STR(pick.zone, "america");
    CHECK(pick.ip == ip_of("10.0.0.1"));

    CHECK(resolver_choose(&cfg, geo, zones, ip_of("80.0.1.1"), NULL, &pick) == 0);
    CHECK_STR(pick.country, "ES");
    CHECK_STR(pick.zone, "europa");
    CHECK(pick.ip == ip_of("10.0.0.2"));

    CHECK(resolver_choose(&cfg, geo, zones, ip_of("133.1.1.1"), NULL, &pick) == 0);
    CHECK_STR(pick.zone, "asia");
    CHECK(pick.ip == ip_of("10.0.0.3"));

    /* IP que no está en la base -> país ZZ -> zona por defecto. */
    CHECK(resolver_choose(&cfg, geo, zones, ip_of("250.1.1.1"), NULL, &pick) == 0);
    CHECK_STR(pick.country, "ZZ");
    CHECK_STR(pick.zone, "america");

    /* País conocido pero sin zona en la tabla (AU) -> zona por defecto. */
    CHECK(resolver_choose(&cfg, geo, zones, ip_of("1.0.0.5"), NULL, &pick) == 0);
    CHECK_STR(pick.country, "AU");
    CHECK_STR(pick.zone, "america");

    /* La zona de un país no está desplegada (falta en DI_ZONE_IPS) -> zona por defecto. */
    setenv("DI_ZONE_IPS", "america=10.0.0.1", 1);
    setenv("DI_DEFAULT_ZONE", "america", 1);
    config_from_env(&cfg, err, sizeof(err));
    CHECK(resolver_choose(&cfg, geo, zones, ip_of("80.0.1.1"), NULL, &pick) == 0);
    CHECK_STR(pick.zone, "america");
    CHECK(pick.ip == ip_of("10.0.0.1"));

    test_failover(geo, zones);

    unsetenv("DI_ZONE_IPS");
    unsetenv("DI_DEFAULT_ZONE");
    geo_free(geo);
    zones_free(zones);
}
