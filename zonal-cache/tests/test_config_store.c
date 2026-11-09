#include "test.h"
#include "../src/config_store.h"

#include <stdlib.h>

static site_config_t *make(const char *domain)
{
    char json[256];
    snprintf(json, sizeof(json), "{\"domains\":[{\"name\":\"%s\",\"maxCacheSize\":1}]}", domain);
    return site_config_parse(json, NULL, 0);
}

TEST(empty_store_returns_null_config)
{
    config_store_t *store = config_store_create();
    config_snapshot_t *snap = config_store_acquire(store);
    CHECK(config_snapshot_get(snap) == NULL);
    config_store_release(store, snap);
    config_store_destroy(store);
}

TEST(old_snapshot_survives_replacement)
{
    config_store_t *store = config_store_create();
    config_store_set(store, make("viejo.com"));
    config_snapshot_t *old = config_store_acquire(store);

    config_store_set(store, make("nuevo.com"));
    /* Un hilo que tomó la configuración vieja la sigue viendo intacta. */
    CHECK_EQ_STR(config_snapshot_get(old)->domains[0].name, "viejo.com");

    config_snapshot_t *cur = config_store_acquire(store);
    CHECK_EQ_STR(config_snapshot_get(cur)->domains[0].name, "nuevo.com");
    config_store_release(store, old);
    config_store_release(store, cur);
    config_store_destroy(store);
}

TEST(refresh_from_file)
{
    char dir[256], path[300];
    test_tmpdir(dir, sizeof(dir));
    snprintf(path, sizeof(path), "%s/config.json", dir);
    FILE *f = fopen(path, "w");
    fputs("{\"domains\":[{\"name\":\"archivo.com\",\"maxCacheSize\":10}]}", f);
    fclose(f);

    settings_t s = {0};
    snprintf(s.config_file, sizeof(s.config_file), "%s", path);
    config_store_t *store = config_store_create();
    char err[128];
    CHECK_EQ_INT(config_store_refresh(store, &s, err, sizeof(err)), 0);
    config_snapshot_t *snap = config_store_acquire(store);
    CHECK_EQ_STR(config_snapshot_get(snap)->domains[0].name, "archivo.com");
    config_store_release(store, snap);

    /* Un archivo inválido no reemplaza la configuración vigente. */
    f = fopen(path, "w");
    fputs("{ roto", f);
    fclose(f);
    CHECK_EQ_INT(config_store_refresh(store, &s, err, sizeof(err)), -1);
    snap = config_store_acquire(store);
    CHECK_EQ_STR(config_snapshot_get(snap)->domains[0].name, "archivo.com");
    config_store_release(store, snap);

    snprintf(s.config_file, sizeof(s.config_file), "%s/no-existe.json", dir);
    CHECK_EQ_INT(config_store_refresh(store, &s, err, sizeof(err)), -1);
    config_store_destroy(store);
}

void suite_config_store(void)
{
    RUN(empty_store_returns_null_config);
    RUN(old_snapshot_survives_replacement);
    RUN(refresh_from_file);
}
