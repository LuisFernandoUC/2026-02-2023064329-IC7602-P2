#include "../src/geo.h"
#include "test.h"

static void test_parse_ipv4(void) {
    uint32_t ip;
    CHECK(geo_parse_ipv4("8.8.4.4", &ip) == 0);
    CHECK(ip == ((8u << 24) | (8u << 16) | (4u << 8) | 4u));
    CHECK(geo_parse_ipv4("255.255.255.255", &ip) == 0 && ip == 0xFFFFFFFFu);
    CHECK(geo_parse_ipv4("256.1.1.1", &ip) == -1);
    CHECK(geo_parse_ipv4("1.2.3", &ip) == -1);
    CHECK(geo_parse_ipv4("1.2.3.4x", &ip) == -1);
    CHECK(geo_parse_ipv4("2001:db8::1", &ip) == -1);
    CHECK(geo_parse_ipv4("", &ip) == -1);
}

static void test_country_lookup(void) {
    geo_db_t *db = geo_load("tests/data/mini-country.csv");
    char cc[3];
    uint32_t ip;

    CHECK(db != NULL);
    if (!db) return;
    CHECK(geo_size(db) == 6); /* el encabezado y la fila IPv6 no cuentan */

    geo_parse_ipv4("8.8.8.8", &ip);
    CHECK(geo_country(db, ip, cc) == 0 && strcmp(cc, "US") == 0);
    geo_parse_ipv4("8.8.8.0", &ip); /* primer IP del rango */
    CHECK(geo_country(db, ip, cc) == 0 && strcmp(cc, "US") == 0);
    geo_parse_ipv4("8.8.8.255", &ip); /* último IP del rango */
    CHECK(geo_country(db, ip, cc) == 0 && strcmp(cc, "US") == 0);
    geo_parse_ipv4("200.0.16.77", &ip);
    CHECK(geo_country(db, ip, cc) == 0 && strcmp(cc, "CU") == 0);
    geo_parse_ipv4("133.5.5.5", &ip); /* viene desordenada en el archivo */
    CHECK(geo_country(db, ip, cc) == 0 && strcmp(cc, "JP") == 0);

    geo_parse_ipv4("8.8.9.1", &ip); /* hueco entre rangos */
    CHECK(geo_country(db, ip, cc) == -1);
    geo_parse_ipv4("250.1.1.1", &ip); /* después del último rango */
    CHECK(geo_country(db, ip, cc) == -1);

    geo_free(db);
}

static void test_missing_files(void) {
    CHECK(geo_load("tests/data/no-existe.csv") == NULL);
    CHECK(zones_load("tests/data/no-existe.csv") == NULL);
    CHECK(geo_size(NULL) == 0);
}

static void test_zone_map(void) {
    zone_map_t *zm = zones_load("tests/data/mini-zones.csv");
    CHECK(zm != NULL);
    if (!zm) return;
    CHECK_STR(zones_lookup(zm, "US"), "america");
    CHECK_STR(zones_lookup(zm, "ES"), "europa");
    CHECK_STR(zones_lookup(zm, "JP"), "asia");
    CHECK(zones_lookup(zm, "ZZ") == NULL);
    CHECK(zones_lookup(zm, "country") == NULL); /* el encabezado no es un país */
    zones_free(zm);
}

void run_geo_tests(void) {
    test_parse_ipv4();
    test_country_lookup();
    test_missing_files();
    test_zone_map();
}
