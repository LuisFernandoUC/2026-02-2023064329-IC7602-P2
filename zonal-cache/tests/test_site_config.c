#include "test.h"
#include "../src/site_config.h"

static const char *SAMPLE =
    "{ \"domains\": ["
    "  { \"name\": \"Apache.P2.local\", \"origin\": \"http://apache:80/\", \"maxCacheSize\": 1000,"
    "    \"urls\": ["
    "      { \"pattern\": \"/*\", \"cacheSize\": 500, \"policy\": \"lfu\", \"auth\": \"none\","
    "        \"fileTypes\": [ {\"extension\": \"html\", \"ttl\": 60}, {\"extension\": \".PNG\", \"ttl\": 3600} ] },"
    "      { \"pattern\": \"/privado/*\", \"cacheSize\": \"200\", \"policy\": \"MRU\", \"auth\": \"user_password\","
    "        \"fileTypes\": [ {\"extension\": \"*\", \"ttl\": 10}, {\"extension\": \"js\", \"ttl\": 0} ] },"
    "      { \"pattern\": \"/api/*\", \"cacheSize\": 100, \"auth\": \"api_key\","
    "        \"apiKeys\": [\"ABCDEF\", {\"id\": \"k2\", \"hash\": \"123456\"}] }"
    "    ] },"
    "  { \"name\": \"*.example.com\", \"maxCacheSize\": 50 },"
    "  { \"domain\": \"www.example.com\", \"maxCacheSize\": 60 }"
    "] }";

TEST(parses_sample_config)
{
    char err[128] = "";
    site_config_t *cfg = site_config_parse(SAMPLE, err, sizeof(err));
    CHECK(cfg != NULL);
    if (!cfg) {
        fprintf(stderr, "    error: %s\n", err);
        return;
    }
    CHECK_EQ_INT(cfg->domain_count, 3);
    const domain_rule_t *d = &cfg->domains[0];
    CHECK_EQ_STR(d->name, "apache.p2.local");
    CHECK_EQ_STR(d->origin, "http://apache:80"); /* sin '/' final */
    CHECK_EQ_INT(d->max_cache_size, 1000);
    CHECK_EQ_INT(d->rule_count, 3);
    CHECK_EQ_INT(d->rules[0].policy, POLICY_LFU);
    CHECK_EQ_INT(d->rules[0].file_type_count, 2);
    CHECK_EQ_STR(d->rules[0].file_types[1].extension, "png");
    CHECK_EQ_INT(d->rules[1].cache_size, 200);
    CHECK_EQ_INT(d->rules[1].auth, AUTH_USER_PASSWORD);
    CHECK_EQ_INT(d->rules[2].policy, POLICY_LRU); /* valor por defecto */
    CHECK_EQ_INT(d->rules[2].auth, AUTH_API_KEY);
    CHECK_EQ_INT(d->rules[2].api_key_count, 2);
    CHECK_EQ_STR(d->rules[2].api_key_hashes[0], "abcdef");
    CHECK_EQ_STR(d->rules[2].api_key_hashes[1], "123456");
    CHECK(cfg->domains[1].origin == NULL);
    CHECK_EQ_STR(cfg->domains[2].name, "www.example.com");
    site_config_free(cfg);
}

TEST(find_domain_prefers_most_specific)
{
    site_config_t *cfg = site_config_parse(SAMPLE, NULL, 0);
    CHECK_EQ_STR(site_config_find_domain(cfg, "www.example.com")->name, "www.example.com");
    CHECK_EQ_STR(site_config_find_domain(cfg, "img.example.com")->name, "*.example.com");
    CHECK_EQ_STR(site_config_find_domain(cfg, "apache.p2.local")->name, "apache.p2.local");
    CHECK(site_config_find_domain(cfg, "otro.com") == NULL);
    CHECK(site_config_find_domain(NULL, "otro.com") == NULL);
    site_config_free(cfg);
}

TEST(find_rule_prefers_most_specific)
{
    site_config_t *cfg = site_config_parse(SAMPLE, NULL, 0);
    const domain_rule_t *d = &cfg->domains[0];
    CHECK_EQ_STR(domain_find_rule(d, "/index.html")->pattern, "/*");
    CHECK_EQ_STR(domain_find_rule(d, "/privado/a.html")->pattern, "/privado/*");
    CHECK_EQ_STR(domain_find_rule(d, "/api/users")->pattern, "/api/*");
    CHECK(domain_find_rule(&cfg->domains[1], "/x") == NULL);
    site_config_free(cfg);
}

TEST(ttl_by_file_type)
{
    site_config_t *cfg = site_config_parse(SAMPLE, NULL, 0);
    const url_rule_t *pub = &cfg->domains[0].rules[0];
    const url_rule_t *priv = &cfg->domains[0].rules[1];
    const url_rule_t *api = &cfg->domains[0].rules[2];
    CHECK_EQ_INT(url_rule_ttl_for_path(pub, "/logo.png"), 3600);
    CHECK_EQ_INT(url_rule_ttl_for_path(pub, "/index.html"), 60);
    CHECK_EQ_INT(url_rule_ttl_for_path(pub, "/"), 60);        /* sin extensión = html */
    CHECK_EQ_INT(url_rule_ttl_for_path(pub, "/app.css"), -1); /* tipo no configurado */
    CHECK_EQ_INT(url_rule_ttl_for_path(priv, "/privado/x.css"), 10); /* comodín */
    CHECK_EQ_INT(url_rule_ttl_for_path(priv, "/privado/x.js"), -1);  /* ttl 0 = no cachear */
    CHECK_EQ_INT(url_rule_ttl_for_path(api, "/api/users"), -1);      /* sin fileTypes */
    site_config_free(cfg);
}

TEST(rejects_invalid_configs)
{
    char err[128];
    CHECK(site_config_parse("no es json", err, sizeof(err)) == NULL);
    CHECK(site_config_parse("{}", err, sizeof(err)) == NULL);
    CHECK(site_config_parse("{\"domains\":[{\"maxCacheSize\":1}]}", err, sizeof(err)) == NULL);
    CHECK(site_config_parse("{\"domains\":[{\"name\":\"a\"}]}", err, sizeof(err)) == NULL);
    CHECK(site_config_parse("{\"domains\":[{\"name\":\"a\",\"maxCacheSize\":-5}]}", err,
                            sizeof(err)) == NULL);
    CHECK(site_config_parse("{\"domains\":[{\"name\":\"a\",\"maxCacheSize\":1,\"urls\":"
                            "[{\"pattern\":\"/*\",\"cacheSize\":1,\"policy\":\"ARC\"}]}]}",
                            err, sizeof(err)) == NULL);
    CHECK(strstr(err, "política") != NULL);
    CHECK(site_config_parse("{\"domains\":[{\"name\":\"a\",\"maxCacheSize\":1,\"urls\":"
                            "[{\"pattern\":\"/*\",\"cacheSize\":1,\"auth\":\"oauth\"}]}]}",
                            err, sizeof(err)) == NULL);
    CHECK(site_config_parse("{\"domains\":[{\"name\":\"a\",\"maxCacheSize\":1,\"urls\":"
                            "[{\"cacheSize\":1}]}]}", err, sizeof(err)) == NULL);
}

TEST(empty_domain_list_is_valid)
{
    site_config_t *cfg = site_config_parse("{\"domains\": []}", NULL, 0);
    CHECK(cfg != NULL);
    CHECK_EQ_INT(cfg ? cfg->domain_count : 99, 0);
    site_config_free(cfg);
}

/* Mismo contenido con los nombres de Docs/Contratos/API-Node.md. */
TEST(accepts_api_node_field_names)
{
    const char *json =
        "{ \"domains\": [ { \"name\": \"img.local\", \"maxCacheBytes\": 2048,"
        "  \"rules\": [ { \"pathPattern\": \"/imagenes/*\", \"maxCacheBytes\": 1024,"
        "    \"authMode\": \"api-key\","
        "    \"fileTypes\": [ { \"extension\": \"png\", \"ttlSeconds\": 3600, \"replacementPolicy\": \"MRU\" } ],"
        "    \"apiKeys\": [ { \"id\": \"k1\", \"keyHash\": \"ABC\" } ] } ] } ] }";
    char err[128] = "";
    site_config_t *cfg = site_config_parse(json, err, sizeof(err));
    CHECK(cfg != NULL);
    if (!cfg) {
        fprintf(stderr, "    error: %s\n", err);
        return;
    }
    const domain_rule_t *d = &cfg->domains[0];
    CHECK_EQ_INT(d->max_cache_size, 2048);
    CHECK_EQ_INT(d->rule_count, 1);
    const url_rule_t *r = &d->rules[0];
    CHECK_EQ_STR(r->pattern, "/imagenes/*");
    CHECK_EQ_INT(r->cache_size, 1024);
    CHECK_EQ_INT(r->auth, AUTH_API_KEY);
    CHECK_EQ_INT(r->policy, POLICY_MRU); /* tomada de replacementPolicy */
    CHECK_EQ_INT(url_rule_ttl_for_path(r, "/imagenes/a.png"), 3600);
    CHECK_EQ_INT(r->api_key_count, 1);
    CHECK_EQ_STR(r->api_key_hashes[0], "abc");
    site_config_free(cfg);
}

TEST(explicit_policy_wins_over_file_type_policy)
{
    const char *json =
        "{ \"domains\": [ { \"name\": \"a\", \"maxCacheSize\": 1, \"urls\": [ { \"pattern\": \"/*\","
        "  \"cacheSize\": 1, \"policy\": \"FIFO\","
        "  \"fileTypes\": [ { \"extension\": \"png\", \"ttl\": 1, \"replacementPolicy\": \"MRU\" } ] } ] } ] }";
    site_config_t *cfg = site_config_parse(json, NULL, 0);
    CHECK(cfg != NULL);
    if (cfg)
        CHECK_EQ_INT(cfg->domains[0].rules[0].policy, POLICY_FIFO);
    site_config_free(cfg);

    const char *bad =
        "{ \"domains\": [ { \"name\": \"a\", \"maxCacheSize\": 1, \"urls\": [ { \"pattern\": \"/*\","
        "  \"cacheSize\": 1, \"fileTypes\": [ { \"extension\": \"png\", \"ttl\": 1, \"replacementPolicy\": \"ARC\" } ] } ] } ] }";
    CHECK(site_config_parse(bad, NULL, 0) == NULL);
}

TEST(auth_mode_names)
{
    CHECK_EQ_INT(auth_mode_from_string("api-key", &(auth_mode_t){0}), 0);
    auth_mode_t m;
    CHECK_EQ_INT(auth_mode_from_string("API_KEY", &m), 0);
    CHECK_EQ_INT(m, AUTH_API_KEY);
    CHECK_EQ_INT(auth_mode_from_string(NULL, &m), 0);
    CHECK_EQ_INT(m, AUTH_NONE);
    CHECK_EQ_INT(auth_mode_from_string("userpass", &m), 0);
    CHECK_EQ_INT(m, AUTH_USER_PASSWORD);
    CHECK_EQ_STR(auth_mode_to_string(AUTH_USER_PASSWORD), "user_password");
}

void suite_site_config(void)
{
    RUN(parses_sample_config);
    RUN(find_domain_prefers_most_specific);
    RUN(find_rule_prefers_most_specific);
    RUN(ttl_by_file_type);
    RUN(rejects_invalid_configs);
    RUN(empty_domain_list_is_valid);
    RUN(accepts_api_node_field_names);
    RUN(explicit_policy_wins_over_file_type_policy);
    RUN(auth_mode_names);
}
