#include "test.h"
#include "../src/wildcard.h"

TEST(exact_match)
{
    CHECK(wildcard_match("/index.html", "/index.html"));
    CHECK(!wildcard_match("/index.html", "/index.htm"));
    CHECK(!wildcard_match("/index.html", "/index.html5"));
}

TEST(star_matches_any_sequence)
{
    CHECK(wildcard_match("/*", "/"));
    CHECK(wildcard_match("/*", "/a/b/c.png"));
    CHECK(wildcard_match("/static/*", "/static/css/app.css"));
    CHECK(!wildcard_match("/static/*", "/public/app.css"));
    CHECK(wildcard_match("*.png", "/img/logo.png"));
    CHECK(!wildcard_match("*.png", "/img/logo.png.bak"));
    CHECK(wildcard_match("/api/*/items", "/api/v1/items"));
    CHECK(wildcard_match("/a*b*c", "/aXXbYYc"));
    CHECK(!wildcard_match("/a*b*c", "/aXXbYY"));
    CHECK(wildcard_match("*", ""));
}

TEST(question_mark_matches_one_char)
{
    CHECK(wildcard_match("/v?/x", "/v1/x"));
    CHECK(!wildcard_match("/v?/x", "/v10/x"));
}

TEST(backtracking)
{
    CHECK(wildcard_match("*ab", "aaab"));
    CHECK(wildcard_match("/*/*.js", "/a/b/c.js"));
    CHECK(!wildcard_match("/*/*.js", "/c.js"));
}

TEST(domain_wildcards_ignore_case)
{
    CHECK(wildcard_match_nocase("*.example.com", "WWW.Example.com"));
    CHECK(!wildcard_match_nocase("*.example.com", "example.com"));
    CHECK(!wildcard_match("*.example.com", "WWW.EXAMPLE.COM"));
}

TEST(specificity_counts_literals)
{
    CHECK_EQ_INT(wildcard_specificity("*"), 0);
    CHECK_EQ_INT(wildcard_specificity("/static/*"), 8);
    CHECK(wildcard_specificity("/static/*.png") > wildcard_specificity("/static/*"));
}

void suite_wildcard(void)
{
    RUN(exact_match);
    RUN(star_matches_any_sequence);
    RUN(question_mark_matches_one_char);
    RUN(backtracking);
    RUN(domain_wildcards_ignore_case);
    RUN(specificity_counts_literals);
}
