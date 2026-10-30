#include "dns.h"

#include <string.h>

#define DNS_HEADER_LEN 12
#define DNS_TYPE_OPT 41
#define EDNS_OPT_ECS 8

static uint16_t rd16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }

static void wr16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xff);
}

/* Salta un nombre en un registro (etiquetas o puntero). Devuelve el nuevo offset o 0 si es inválido. */
static size_t skip_name(const uint8_t *buf, size_t len, size_t off) {
    while (off < len) {
        uint8_t l = buf[off];
        if (l == 0) return off + 1;
        if ((l & 0xC0) == 0xC0) return off + 2 <= len ? off + 2 : 0;
        if (l & 0xC0) return 0;
        off += 1 + (size_t)l;
    }
    return 0;
}

/* Busca la opción EDNS Client Subnet en los registros Additional. */
static void parse_ecs(const uint8_t *buf, size_t len, size_t off, uint16_t arcount, dns_query_t *q) {
    for (uint16_t i = 0; i < arcount; i++) {
        off = skip_name(buf, len, off);
        if (off == 0 || off + 10 > len) return;
        uint16_t type = rd16(buf + off);
        uint16_t rdlen = rd16(buf + off + 8);
        off += 10;
        if (off + rdlen > len) return;
        if (type == DNS_TYPE_OPT) {
            size_t o = off, end = off + rdlen;
            while (o + 4 <= end) {
                uint16_t code = rd16(buf + o);
                uint16_t olen = rd16(buf + o + 2);
                o += 4;
                if (o + olen > end) return;
                if (code == EDNS_OPT_ECS && olen >= 4 && rd16(buf + o) == 1) {
                    unsigned srcmask = buf[o + 2];
                    size_t nbytes = (srcmask + 7) / 8;
                    if (nbytes <= 4 && 4 + nbytes <= olen) {
                        uint8_t addr[4] = {0, 0, 0, 0};
                        memcpy(addr, buf + o + 4, nbytes);
                        q->ecs_ip = ((uint32_t)addr[0] << 24) | ((uint32_t)addr[1] << 16) |
                                    ((uint32_t)addr[2] << 8) | addr[3];
                        q->has_ecs = 1;
                        return;
                    }
                }
                o += olen;
            }
        }
        off += rdlen;
    }
}

int dns_parse_query(const uint8_t *buf, size_t len, dns_query_t *q) {
    if (len < DNS_HEADER_LEN + 5) return -1;
    memset(q, 0, sizeof(*q));

    uint8_t flags_hi = buf[2];
    if (flags_hi & 0x80) return -1;               /* es una respuesta, no una consulta */
    if (((flags_hi >> 3) & 0x0F) != 0) return -1; /* solo OPCODE QUERY */
    if (rd16(buf + 4) != 1) return -1;            /* un único Question */

    size_t off = DNS_HEADER_LEN, n = 0;
    for (;;) {
        if (off >= len) return -1;
        uint8_t l = buf[off++];
        if (l == 0) break;
        if (l & 0xC0) return -1; /* compresión no válida en el Question */
        if (off + l > len || n + l + 1 > DNS_MAX_NAME) return -1;
        if (n > 0) q->name[n++] = '.';
        for (uint8_t i = 0; i < l; i++) {
            char c = (char)buf[off + i];
            q->name[n++] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
        }
        off += l;
    }
    q->name[n] = '\0';
    if (n == 0 || off + 4 > len) return -1;

    q->qtype = rd16(buf + off);
    q->qclass = rd16(buf + off + 2);
    q->question_end = off + 4;

    /* Los registros Answer y Authority no existen en una consulta; solo se lee Additional. */
    if (rd16(buf + 6) == 0 && rd16(buf + 8) == 0 && rd16(buf + 10) > 0)
        parse_ecs(buf, len, q->question_end, rd16(buf + 10), q);
    return 0;
}

/* Copia header + Question y deja las banderas de una respuesta sin registros. */
static size_t start_response(const uint8_t *req, const dns_query_t *q, int rcode, uint8_t *out,
                             size_t outcap) {
    if (q->question_end > outcap) return 0;
    memcpy(out, req, q->question_end);
    out[2] = (uint8_t)(0x80 | (req[2] & 0x01)); /* QR=1 y se conserva RD */
    out[3] = (uint8_t)(0x80 | (rcode & 0x0F));  /* RA=1 */
    memset(out + 6, 0, 6);                      /* ANCOUNT, NSCOUNT y ARCOUNT = 0 (se descarta el OPT) */
    return q->question_end;
}

size_t dns_build_empty_response(const uint8_t *req, const dns_query_t *q, int rcode, uint8_t *out,
                                size_t outcap) {
    return start_response(req, q, rcode, out, outcap);
}

size_t dns_build_a_response(const uint8_t *req, const dns_query_t *q, uint32_t ip, uint32_t ttl,
                            uint8_t *out, size_t outcap) {
    size_t n = start_response(req, q, DNS_RCODE_NOERROR, out, outcap);
    if (n == 0 || n + 16 > outcap) return 0;

    wr16(out + 6, 1); /* ANCOUNT = 1 */
    out[n++] = 0xC0;  /* NAME: puntero al nombre del Question (offset 12) */
    out[n++] = 0x0C;
    wr16(out + n, DNS_TYPE_A);
    n += 2;
    wr16(out + n, DNS_CLASS_IN);
    n += 2;
    out[n++] = (uint8_t)(ttl >> 24);
    out[n++] = (uint8_t)(ttl >> 16);
    out[n++] = (uint8_t)(ttl >> 8);
    out[n++] = (uint8_t)ttl;
    wr16(out + n, 4); /* RDLENGTH */
    n += 2;
    out[n++] = (uint8_t)(ip >> 24);
    out[n++] = (uint8_t)(ip >> 16);
    out[n++] = (uint8_t)(ip >> 8);
    out[n++] = (uint8_t)ip;
    return n;
}
