#include "test.h"
#include "../src/settings.h"

#include <stdlib.h>

static const char *VARS[] = {
    "ZC_ZONE", "ZC_HTTP_PORT", "ZC_HTTPS_PORT", "ZC_TLS_CERT_FILE", "ZC_TLS_KEY_FILE",
    "ZC_CACHE_DIR", "ZC_CONFIG_URL", "ZC_CONFIG_TOKEN", "ZC_CONFIG_FILE",
    "ZC_CONFIG_REFRESH_SECONDS", "ZC_LOGIN_URL", "ZC_SESSION_SECRET",
    "ZC_ORIGIN_TIMEOUT_SECONDS", "ZC_ORIGIN_VERIFY_TLS", "ZC_MAX_BODY_BYTES", "ZC_LOG_LEVEL",
};

static void clear_env(void)
{
    for (size_t i = 0; i < sizeof(VARS) / sizeof(VARS[0]); i++)
        unsetenv(VARS[i]);
}

TEST(defaults_with_config_url)
{
    clear_env();
    setenv("ZC_CONFIG_URL", "https://api.example.com/cache/config", 1);
    settings_t s;
    char err[128];
    CHECK_EQ_INT(settings_load(&s, err, sizeof(err)), 0);
    CHECK_EQ_INT(s.http_port, 8080);
    CHECK_EQ_INT(s.https_port, 0);
    CHECK_EQ_INT(s.config_refresh_seconds, 30);
    CHECK_EQ_INT(s.origin_verify_tls, 1);
    CHECK_EQ_STR(s.zone, "default");
    CHECK_EQ_STR(s.cache_dir, "/var/cache/zonal-cache");
    CHECK_EQ_STR(s.config_url, "https://api.example.com/cache/config");
    clear_env();
}

TEST(reads_all_variables)
{
    clear_env();
    setenv("ZC_ZONE", "america", 1);
    setenv("ZC_HTTP_PORT", "80", 1);
    setenv("ZC_HTTPS_PORT", "443", 1);
    setenv("ZC_TLS_CERT_FILE", "/tls/tls.crt", 1);
    setenv("ZC_TLS_KEY_FILE", "/tls/tls.key", 1);
    setenv("ZC_CONFIG_FILE", "/etc/zc/config.json", 1);
    setenv("ZC_ORIGIN_VERIFY_TLS", "0", 1);
    setenv("ZC_MAX_BODY_BYTES", "2048", 1);
    setenv("ZC_SESSION_SECRET", "abc", 1);
    settings_t s;
    char err[128];
    CHECK_EQ_INT(settings_load(&s, err, sizeof(err)), 0);
    CHECK_EQ_STR(s.zone, "america");
    CHECK_EQ_INT(s.http_port, 80);
    CHECK_EQ_INT(s.https_port, 443);
    CHECK_EQ_INT(s.origin_verify_tls, 0);
    CHECK_EQ_INT(s.max_body_bytes, 2048);
    CHECK_EQ_STR(s.session_secret, "abc");
    clear_env();
}

TEST(rejects_invalid_values)
{
    settings_t s;
    char err[128];

    clear_env();
    CHECK_EQ_INT(settings_load(&s, err, sizeof(err)), -1); /* sin fuente de configuración */

    setenv("ZC_CONFIG_FILE", "/x.json", 1);
    setenv("ZC_HTTP_PORT", "70000", 1);
    CHECK_EQ_INT(settings_load(&s, err, sizeof(err)), -1);
    CHECK(strstr(err, "ZC_HTTP_PORT") != NULL);

    setenv("ZC_HTTP_PORT", "80a", 1);
    CHECK_EQ_INT(settings_load(&s, err, sizeof(err)), -1);

    setenv("ZC_HTTP_PORT", "8080", 1);
    setenv("ZC_HTTPS_PORT", "8443", 1); /* sin certificado */
    CHECK_EQ_INT(settings_load(&s, err, sizeof(err)), -1);

    setenv("ZC_TLS_CERT_FILE", "/c", 1);
    setenv("ZC_TLS_KEY_FILE", "/k", 1);
    setenv("ZC_HTTPS_PORT", "8080", 1); /* mismo puerto que HTTP */
    CHECK_EQ_INT(settings_load(&s, err, sizeof(err)), -1);
    clear_env();
}

void suite_settings(void)
{
    RUN(defaults_with_config_url);
    RUN(reads_all_variables);
    RUN(rejects_invalid_values);
}
