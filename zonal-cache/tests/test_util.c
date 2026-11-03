#include "test.h"
#include "../src/util.h"

#include <stdlib.h>

TEST(strlcpy_truncates_and_terminates)
{
    char buf[5];
    CHECK_EQ_INT(zc_strlcpy(buf, "abcdefgh", sizeof(buf)), 8);
    CHECK_EQ_STR(buf, "abcd");
}

TEST(strcaseeq_ignores_case)
{
    CHECK(zc_strcaseeq("Content-Type", "content-type"));
    CHECK(!zc_strcaseeq("Host", "Hostname"));
}

TEST(trim_removes_spaces)
{
    char s[] = "  hola mundo \t ";
    CHECK_EQ_STR(zc_str_trim(s), "hola mundo");
}

TEST(path_extension_cases)
{
    char ext[16];
    zc_path_extension("/img/logo.PNG", ext, sizeof(ext));
    CHECK_EQ_STR(ext, "png");
    zc_path_extension("/a.b/c.css?v=1.2", ext, sizeof(ext));
    CHECK_EQ_STR(ext, "css");
    zc_path_extension("/api/users", ext, sizeof(ext));
    CHECK_EQ_STR(ext, "");
    zc_path_extension("/dir.v2/", ext, sizeof(ext));
    CHECK_EQ_STR(ext, "");
    zc_path_extension("/archivo.", ext, sizeof(ext));
    CHECK_EQ_STR(ext, "");
}

TEST(fnv_is_deterministic)
{
    CHECK(zc_fnv1a64("abc", 3) == zc_fnv1a64("abc", 3));
    CHECK(zc_fnv1a64("abc", 3) != zc_fnv1a64("abd", 3));
    /* Vector conocido de FNV-1a 64: "a" -> af63dc4c8601ec8c */
    CHECK(zc_fnv1a64("a", 1) == 0xaf63dc4c8601ec8cULL);
}

TEST(base64url_roundtrip)
{
    const char *inputs[] = {"", "f", "fo", "foo", "foob", "fooba", "foobar", "\xff\xfe?"};
    for (size_t i = 0; i < sizeof(inputs) / sizeof(inputs[0]); i++) {
        char enc[64];
        unsigned char dec[64];
        size_t len = strlen(inputs[i]);
        zc_base64url_encode((const unsigned char *)inputs[i], len, enc);
        long n = zc_base64url_decode(enc, dec, sizeof(dec));
        CHECK_EQ_INT(n, (long)len);
        CHECK(memcmp(dec, inputs[i], len) == 0);
    }
    char enc[16];
    zc_base64url_encode((const unsigned char *)"foobar", 6, enc);
    CHECK_EQ_STR(enc, "Zm9vYmFy");
    zc_base64url_encode((const unsigned char *)"\xfb\xff", 2, enc);
    CHECK_EQ_STR(enc, "-_8");
}

TEST(base64url_rejects_invalid)
{
    unsigned char out[16];
    CHECK_EQ_INT(zc_base64url_decode("ab+c", out, sizeof(out)), -1);
    CHECK_EQ_INT(zc_base64url_decode("abcde", out, sizeof(out)), -1);
}

TEST(url_encode_decode)
{
    char *enc = zc_url_encode("/a b?x=1&y=ñ");
    CHECK_EQ_STR(enc, "%2Fa%20b%3Fx%3D1%26y%3D%C3%B1");
    char copy[64];
    zc_strlcpy(copy, enc, sizeof(copy));
    zc_url_decode(copy);
    CHECK_EQ_STR(copy, "/a b?x=1&y=ñ");
    free(enc);
}

TEST(query_param_lookup)
{
    char v[64];
    CHECK(zc_query_param("token=abc&next=%2Fhome", "next", v, sizeof(v)));
    CHECK_EQ_STR(v, "/home");
    CHECK(zc_query_param("token=abc&next=%2Fhome", "token", v, sizeof(v)));
    CHECK_EQ_STR(v, "abc");
    CHECK(!zc_query_param("tokens=abc", "token", v, sizeof(v)));
    CHECK(!zc_query_param(NULL, "token", v, sizeof(v)));
}

void suite_util(void)
{
    RUN(strlcpy_truncates_and_terminates);
    RUN(strcaseeq_ignores_case);
    RUN(trim_removes_spaces);
    RUN(path_extension_cases);
    RUN(fnv_is_deterministic);
    RUN(base64url_roundtrip);
    RUN(base64url_rejects_invalid);
    RUN(url_encode_decode);
    RUN(query_param_lookup);
}
