#include "../src/dns.h"
#include "test.h"

/* Consulta A para "Apache.P2.local" (con mayúsculas a propósito), RD=1, sin EDNS. */
static const uint8_t QUERY_A[] = {
    0x12, 0x34, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    6, 'A', 'p', 'a', 'c', 'h', 'e', 2, 'P', '2', 5, 'l', 'o', 'c', 'a', 'l', 0,
    0x00, 0x01, 0x00, 0x01};

/* La misma consulta con un registro OPT que trae EDNS Client Subnet 200.0.16.0/24. */
static const uint8_t QUERY_ECS[] = {
    0x12, 0x34, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
    6, 'a', 'p', 'a', 'c', 'h', 'e', 2, 'p', '2', 5, 'l', 'o', 'c', 'a', 'l', 0,
    0x00, 0x01, 0x00, 0x01,
    0x00,                   /* NAME: raíz */
    0x00, 0x29,             /* TYPE: OPT */
    0x10, 0x00,             /* tamaño UDP */
    0x00, 0x00, 0x00, 0x00, /* TTL */
    0x00, 0x0B,             /* RDLENGTH: 11 */
    0x00, 0x08, 0x00, 0x07, /* opción ECS, longitud 7 */
    0x00, 0x01, 24, 0,      /* IPv4, /24 */
    200, 0, 16};

static void test_parse_basic(void) {
    dns_query_t q;
    CHECK(dns_parse_query(QUERY_A, sizeof(QUERY_A), &q) == 0);
    CHECK_STR(q.name, "apache.p2.local"); /* pasa a minúsculas */
    CHECK(q.qtype == DNS_TYPE_A);
    CHECK(q.qclass == DNS_CLASS_IN);
    CHECK(q.question_end == sizeof(QUERY_A));
    CHECK(q.has_ecs == 0);
}

static void test_parse_rejects_invalid(void) {
    dns_query_t q;
    uint8_t buf[sizeof(QUERY_A)];

    CHECK(dns_parse_query(QUERY_A, 10, &q) == -1); /* más corta que el header */

    memcpy(buf, QUERY_A, sizeof(buf));
    buf[2] |= 0x80; /* QR=1: es una respuesta */
    CHECK(dns_parse_query(buf, sizeof(buf), &q) == -1);

    memcpy(buf, QUERY_A, sizeof(buf));
    buf[2] |= 0x10; /* OPCODE != 0 */
    CHECK(dns_parse_query(buf, sizeof(buf), &q) == -1);

    memcpy(buf, QUERY_A, sizeof(buf));
    buf[5] = 2; /* QDCOUNT = 2 */
    CHECK(dns_parse_query(buf, sizeof(buf), &q) == -1);

    memcpy(buf, QUERY_A, sizeof(buf));
    buf[12] = 60; /* una etiqueta más larga que el paquete */
    CHECK(dns_parse_query(buf, sizeof(buf), &q) == -1);

    CHECK(dns_parse_query(QUERY_A, sizeof(QUERY_A) - 3, &q) == -1); /* sin TYPE/CLASS completos */
}

static void test_parse_ecs(void) {
    dns_query_t q;
    CHECK(dns_parse_query(QUERY_ECS, sizeof(QUERY_ECS), &q) == 0);
    CHECK(q.has_ecs == 1);
    CHECK(q.ecs_ip == ((200u << 24) | (0u << 16) | (16u << 8)));
    CHECK(q.question_end == sizeof(QUERY_A));
}

static void test_ecs_truncated_is_ignored(void) {
    dns_query_t q;
    CHECK(dns_parse_query(QUERY_ECS, sizeof(QUERY_ECS) - 2, &q) == 0);
    CHECK(q.has_ecs == 0);
}

static void test_build_a_response(void) {
    dns_query_t q;
    uint8_t out[DNS_MAX_PACKET];
    dns_parse_query(QUERY_ECS, sizeof(QUERY_ECS), &q);

    size_t n = dns_build_a_response(QUERY_ECS, &q, (10u << 24) | (96u << 16) | (0u << 8) | 7u, 30, out,
                                    sizeof(out));
    CHECK(n == q.question_end + 16);
    CHECK(out[0] == 0x12 && out[1] == 0x34);       /* conserva el ID */
    CHECK((out[2] & 0x80) != 0 && (out[2] & 0x01)); /* QR=1 y RD conservado */
    CHECK((out[3] & 0x0F) == 0);                    /* RCODE = 0 */
    CHECK(out[6] == 0 && out[7] == 1);              /* ANCOUNT = 1 */
    CHECK(out[10] == 0 && out[11] == 0);            /* se descarta el OPT */
    CHECK(out[q.question_end] == 0xC0 && out[q.question_end + 1] == 0x0C);
    CHECK(out[q.question_end + 9] == 30); /* TTL */
    CHECK(out[n - 6] == 0 && out[n - 5] == 4); /* RDLENGTH */
    CHECK(out[n - 4] == 10 && out[n - 3] == 96 && out[n - 2] == 0 && out[n - 1] == 7);
}

static void test_build_empty_response(void) {
    dns_query_t q;
    uint8_t out[DNS_MAX_PACKET];
    dns_parse_query(QUERY_A, sizeof(QUERY_A), &q);

    size_t n = dns_build_empty_response(QUERY_A, &q, DNS_RCODE_SERVFAIL, out, sizeof(out));
    CHECK(n == q.question_end);
    CHECK((out[3] & 0x0F) == DNS_RCODE_SERVFAIL);
    CHECK(out[6] == 0 && out[7] == 0);
}

static void test_build_fails_when_buffer_is_small(void) {
    dns_query_t q;
    uint8_t out[20];
    dns_parse_query(QUERY_A, sizeof(QUERY_A), &q);
    CHECK(dns_build_a_response(QUERY_A, &q, 1, 30, out, sizeof(out)) == 0);
    CHECK(dns_build_empty_response(QUERY_A, &q, 0, out, sizeof(out)) == 0);
}

void run_dns_tests(void) {
    test_parse_basic();
    test_parse_rejects_invalid();
    test_parse_ecs();
    test_ecs_truncated_is_ignored();
    test_build_a_response();
    test_build_empty_response();
    test_build_fails_when_buffer_is_small();
}
