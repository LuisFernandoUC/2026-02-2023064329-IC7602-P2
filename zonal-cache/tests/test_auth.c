#include "test.h"
#include "../src/auth.h"

#include <stdlib.h>

/*
 * Token generado con Node.js (crypto.createHmac) tal como lo emitiría el API:
 *   secreto: "secreto-de-prueba"
 *   payload: {"domain":"apache.p2.local","sub":"ana","iat":1700000000,"exp":4102444800}
 */
static const char *NODE_TOKEN =
    "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
    "eyJkb21haW4iOiJhcGFjaGUucDIubG9jYWwiLCJzdWIiOiJhbmEiLCJpYXQiOjE3MDAwMDAwMDAsImV4cCI6NDEwMjQ0NDgwMH0."
    "J6QiHHWXwwq9Ap-Cm4vDuRuWdF1aPOWPDLIVWy1Tq_o";

TEST(sha256_known_vectors)
{
    char hex[65];
    auth_sha256_hex("abc", 3, hex);
    CHECK_EQ_STR(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    auth_sha256_hex("clave-valida", 12, hex); /* calculado con Node.js */
    CHECK_EQ_STR(hex, "e2bc022cb9c8b0dfd4db95058c79f8422bdecec430dd3907c945509a6c0926bc");
}

TEST(secure_equals)
{
    CHECK(auth_secure_equals("abc", "abc"));
    CHECK(!auth_secure_equals("abc", "abd"));
    CHECK(!auth_secure_equals("abc", "abcd"));
    CHECK(auth_secure_equals("", ""));
}

TEST(api_key_validation)
{
    char *hashes[] = {"0000", "e2bc022cb9c8b0dfd4db95058c79f8422bdecec430dd3907c945509a6c0926bc"};
    CHECK(auth_api_key_valid("clave-valida", hashes, 2));
    CHECK(!auth_api_key_valid("clave-invalida", hashes, 2));
    CHECK(!auth_api_key_valid("", hashes, 2));
    CHECK(!auth_api_key_valid(NULL, hashes, 2));
    CHECK(!auth_api_key_valid("clave-valida", hashes, 0));
}

TEST(verifies_token_issued_by_node)
{
    char user[32] = "";
    time_t exp = 0;
    CHECK_EQ_INT(auth_token_verify("secreto-de-prueba", NODE_TOKEN, "Apache.P2.Local", 1800000000,
                                   user, sizeof(user), &exp),
                 TOKEN_OK);
    CHECK_EQ_STR(user, "ana");
    CHECK_EQ_INT(exp, 4102444800LL);
}

TEST(token_roundtrip)
{
    char *tok = auth_token_create("s3cr3t", "a.com", "luis", 2000);
    CHECK(tok != NULL);
    char user[32];
    CHECK_EQ_INT(auth_token_verify("s3cr3t", tok, "a.com", 1000, user, sizeof(user), NULL),
                 TOKEN_OK);
    CHECK_EQ_STR(user, "luis");
    free(tok);
}

TEST(token_rejections)
{
    char *tok = auth_token_create("s3cr3t", "a.com", "luis", 2000);
    CHECK_EQ_INT(auth_token_verify("otro", tok, "a.com", 1000, NULL, 0, NULL), TOKEN_BAD_SIGNATURE);
    CHECK_EQ_INT(auth_token_verify("s3cr3t", tok, "a.com", 2000, NULL, 0, NULL), TOKEN_EXPIRED);
    CHECK_EQ_INT(auth_token_verify("s3cr3t", tok, "b.com", 1000, NULL, 0, NULL),
                 TOKEN_WRONG_DOMAIN);
    CHECK_EQ_INT(auth_token_verify("", tok, "a.com", 1000, NULL, 0, NULL), TOKEN_MALFORMED);

    /* Firma alterada */
    char *bad = strdup(tok);
    char *last = bad + strlen(bad) - 1;
    *last = *last == 'A' ? 'B' : 'A';
    CHECK_EQ_INT(auth_token_verify("s3cr3t", bad, "a.com", 1000, NULL, 0, NULL),
                 TOKEN_BAD_SIGNATURE);
    free(bad);
    free(tok);

    CHECK_EQ_INT(auth_token_verify("s3cr3t", "a.b", "a.com", 1000, NULL, 0, NULL),
                 TOKEN_MALFORMED);
    CHECK_EQ_INT(auth_token_verify("s3cr3t", "a.b.c.d", "a.com", 1000, NULL, 0, NULL),
                 TOKEN_MALFORMED);
    CHECK_EQ_INT(auth_token_verify("s3cr3t", NULL, "a.com", 1000, NULL, 0, NULL), TOKEN_MALFORMED);
}

TEST(rejects_alg_none)
{
    /* {"alg":"none"} . {"domain":"a.com","sub":"x","exp":9999999999} . (sin firma) */
    const char *tok = "eyJhbGciOiJub25lIn0."
                      "eyJkb21haW4iOiJhLmNvbSIsInN1YiI6IngiLCJleHAiOjk5OTk5OTk5OTl9.";
    CHECK_EQ_INT(auth_token_verify("s3cr3t", tok, "a.com", 1000, NULL, 0, NULL), TOKEN_MALFORMED);
}

void suite_auth(void)
{
    RUN(sha256_known_vectors);
    RUN(secure_equals);
    RUN(api_key_validation);
    RUN(verifies_token_issued_by_node);
    RUN(token_roundtrip);
    RUN(token_rejections);
    RUN(rejects_alg_none);
}
