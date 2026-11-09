#include "test.h"
#include "../src/http.h"

#include <stdlib.h>

static long parse(const char *raw, http_request_t *req)
{
    return http_parse_request_head(raw, strlen(raw), req);
}

TEST(parses_request_line_and_headers)
{
    const char *raw = "GET /img/logo.png?v=2 HTTP/1.1\r\n"
                      "Host: Apache.P2.local:8080\r\n"
                      "X-Api-Key:  secreto  \r\n"
                      "\r\n";
    http_request_t req;
    CHECK_EQ_INT(parse(raw, &req), (long)strlen(raw));
    CHECK_EQ_STR(req.method, "GET");
    CHECK_EQ_STR(req.target, "/img/logo.png?v=2");
    CHECK_EQ_STR(req.path, "/img/logo.png");
    CHECK_EQ_STR(req.query, "v=2");
    CHECK_EQ_STR(req.version, "HTTP/1.1");
    CHECK_EQ_INT(req.header_count, 2);
    CHECK_EQ_STR(http_request_header(&req, "x-api-key"), "secreto");
    char host[64];
    CHECK_EQ_INT(http_request_host(&req, host, sizeof(host)), 0);
    CHECK_EQ_STR(host, "apache.p2.local");
    http_request_free(&req);
}

TEST(request_without_headers)
{
    http_request_t req;
    CHECK(parse("GET / HTTP/1.0\r\n\r\n", &req) > 0);
    CHECK(req.query == NULL);
    CHECK_EQ_INT(req.header_count, 0);
    char host[16];
    CHECK_EQ_INT(http_request_host(&req, host, sizeof(host)), -1);
    http_request_free(&req);
}

TEST(incomplete_head_returns_zero)
{
    http_request_t req;
    CHECK_EQ_INT(parse("GET / HTTP/1.1\r\nHost: a\r\n", &req), 0);
}

TEST(malformed_requests_rejected)
{
    http_request_t req;
    CHECK_EQ_INT(parse("GET /\r\n\r\n", &req), -1);
    CHECK_EQ_INT(parse("GET / HTTP/1.1 extra\r\n\r\n", &req), -1);
    CHECK_EQ_INT(parse("GET http://x/ HTTP/1.1\r\n\r\n", &req), -1);
    CHECK_EQ_INT(parse("GET / FTP/1.0\r\n\r\n", &req), -1);
    CHECK_EQ_INT(parse("GET / HTTP/1.1\r\nSinDosPuntos\r\n\r\n", &req), -1);
    CHECK_EQ_INT(parse("G(T / HTTP/1.1\r\n\r\n", &req), -1);
}

TEST(content_length_and_body)
{
    const char *raw = "POST /api/items HTTP/1.1\r\nHost: a\r\nContent-Length: 7\r\n\r\n{\"a\":1}";
    http_request_t req;
    long head = parse(raw, &req);
    CHECK(head > 0);
    CHECK_EQ_INT(http_request_content_length(&req), 7);
    CHECK_EQ_INT(http_request_set_body(&req, (const unsigned char *)raw + head, 7), 0);
    CHECK_EQ_INT(req.body_len, 7);
    CHECK(memcmp(req.body, "{\"a\":1}", 7) == 0);
    http_request_free(&req);

    CHECK(parse("POST / HTTP/1.1\r\nContent-Length: abc\r\n\r\n", &req) > 0);
    CHECK_EQ_INT(http_request_content_length(&req), -1);
    http_request_free(&req);
}

TEST(cookie_lookup)
{
    http_request_t req;
    CHECK(parse("GET / HTTP/1.1\r\nCookie: a=1; zc_session=tok.en.x; b=2\r\n\r\n", &req) > 0);
    char v[64];
    CHECK(http_request_cookie(&req, "zc_session", v, sizeof(v)));
    CHECK_EQ_STR(v, "tok.en.x");
    CHECK(http_request_cookie(&req, "b", v, sizeof(v)));
    CHECK_EQ_STR(v, "2");
    CHECK(!http_request_cookie(&req, "zc", v, sizeof(v)));
    http_request_free(&req);
}

TEST(response_serialization)
{
    http_response_t resp;
    http_response_init(&resp, 200);
    http_response_add_header(&resp, "Content-Type", "text/plain");
    http_response_add_header(&resp, "Content-Length", "999"); /* se ignora */
    http_response_set_body(&resp, "hola", 4);
    size_t len = 0;
    char *raw = http_response_serialize(&resp, &len);
    const char *expected = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n"
                           "Content-Length: 4\r\nConnection: close\r\n\r\nhola";
    CHECK_EQ_INT(len, strlen(expected));
    CHECK(raw && memcmp(raw, expected, len) == 0);
    free(raw);
    CHECK_EQ_STR(http_response_header(&resp, "content-type"), "text/plain");
    http_response_free(&resp);
}

TEST(redirect_and_text_helpers)
{
    http_response_t resp;
    http_response_init(&resp, 200);
    http_response_redirect(&resp, "/login");
    CHECK_EQ_INT(resp.status, 302);
    CHECK_EQ_STR(http_response_header(&resp, "Location"), "/login");
    http_response_text(&resp, 404, "no");
    CHECK_EQ_INT(resp.status, 404);
    CHECK(http_response_header(&resp, "Location") == NULL);
    CHECK_EQ_INT(resp.body_len, 2);
    http_response_free(&resp);
}

TEST(hop_by_hop_headers)
{
    CHECK(http_is_hop_by_hop("connection"));
    CHECK(http_is_hop_by_hop("Transfer-Encoding"));
    CHECK(!http_is_hop_by_hop("Content-Type"));
    CHECK_EQ_STR(http_status_text(404), "Not Found");
}

void suite_http(void)
{
    RUN(parses_request_line_and_headers);
    RUN(request_without_headers);
    RUN(incomplete_head_returns_zero);
    RUN(malformed_requests_rejected);
    RUN(content_length_and_body);
    RUN(cookie_lookup);
    RUN(response_serialization);
    RUN(redirect_and_text_helpers);
    RUN(hop_by_hop_headers);
}
