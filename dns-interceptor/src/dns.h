#ifndef DNS_H
#define DNS_H

#include <stddef.h>
#include <stdint.h>

/* Tamaño máximo de un paquete DNS sobre UDP que se maneja (RFC 1035 + EDNS0). */
#define DNS_MAX_PACKET 1232
#define DNS_MAX_NAME 255

/* Códigos de respuesta (RCODE) usados por el interceptor. */
#define DNS_RCODE_NOERROR 0
#define DNS_RCODE_SERVFAIL 2

#define DNS_TYPE_A 1
#define DNS_CLASS_IN 1

typedef struct {
    char name[DNS_MAX_NAME + 1]; /* dominio consultado, en minúsculas y sin punto final */
    uint16_t qtype;
    uint16_t qclass;
    size_t question_end; /* offset donde termina la sección Question */
    int has_ecs;         /* 1 si la consulta trae EDNS Client Subnet (RFC 7871) con IPv4 */
    uint32_t ecs_ip;     /* dirección de la subred del cliente, en orden de host */
} dns_query_t;

/* Interpreta una consulta estándar (QR=0, OPCODE=0, un solo Question).
 * Devuelve 0 si es válida y -1 en caso contrario. */
int dns_parse_query(const uint8_t *buf, size_t len, dns_query_t *q);

/* Construye una respuesta con un registro A (TTL en segundos, ip en orden de host).
 * Devuelve el tamaño escrito en out, o 0 si no cabe. */
size_t dns_build_a_response(const uint8_t *req, const dns_query_t *q, uint32_t ip,
                            uint32_t ttl, uint8_t *out, size_t outcap);

/* Construye una respuesta sin registros (NODATA con RCODE=0, o error con otro RCODE). */
size_t dns_build_empty_response(const uint8_t *req, const dns_query_t *q, int rcode,
                                uint8_t *out, size_t outcap);

#endif
