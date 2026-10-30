#include <stdlib.h>

#include "../src/resolver.h"
#include "test.h"

static uint32_t ip_of(const char *text) {
    uint32_t ip = 0;
    geo_parse_ipv4(text, &ip);
    return ip;
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
    CHECK(resolver_choose(&cfg, geo, zones, ip_of("8.8.8.8"), &pick) == 0);
    CHECK_STR(pick.country, "US");
    CHECK_STR(pick.zone, "america");
    CHECK(pick.ip == ip_of("10.0.0.1"));

    CHECK(resolver_choose(&cfg, geo, zones, ip_of("80.0.1.1"), &pick) == 0);
    CHECK_STR(pick.country, "ES");
    CHECK_STR(pick.zone, "europa");
    CHECK(pick.ip == ip_of("10.0.0.2"));

    CHECK(resolver_choose(&cfg, geo, zones, ip_of("133.1.1.1"), &pick) == 0);
    CHECK_STR(pick.zone, "asia");
    CHECK(pick.ip == ip_of("10.0.0.3"));

    /* IP que no está en la base -> país ZZ -> zona por defecto. */
    CHECK(resolver_choose(&cfg, geo, zones, ip_of("250.1.1.1"), &pick) == 0);
    CHECK_STR(pick.country, "ZZ");
    CHECK_STR(pick.zone, "america");

    /* País conocido pero sin zona en la tabla (AU) -> zona por defecto. */
    CHECK(resolver_choose(&cfg, geo, zones, ip_of("1.0.0.5"), &pick) == 0);
    CHECK_STR(pick.country, "AU");
    CHECK_STR(pick.zone, "america");

    /* La zona de un país no está desplegada (falta en DI_ZONE_IPS) -> zona por defecto. */
    setenv("DI_ZONE_IPS", "america=10.0.0.1", 1);
    setenv("DI_DEFAULT_ZONE", "america", 1);
    config_from_env(&cfg, err, sizeof(err));
    CHECK(resolver_choose(&cfg, geo, zones, ip_of("80.0.1.1"), &pick) == 0);
    CHECK_STR(pick.zone, "america");
    CHECK(pick.ip == ip_of("10.0.0.1"));

    unsetenv("DI_ZONE_IPS");
    unsetenv("DI_DEFAULT_ZONE");
    geo_free(geo);
    zones_free(zones);
}
