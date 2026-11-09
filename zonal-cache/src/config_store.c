/*
 * config_store.c - Snapshots con conteo de referencias y carga de la configuración.
 */
#include "config_store.h"
#include "origin.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct config_snapshot {
    site_config_t *cfg;
    int refs;
};

struct config_store {
    pthread_mutex_t lock;
    config_snapshot_t *current;
};

config_store_t *config_store_create(void)
{
    config_store_t *store = calloc(1, sizeof(*store));
    if (store)
        pthread_mutex_init(&store->lock, NULL);
    return store;
}

static void snapshot_unref(config_snapshot_t *snap)
{
    if (snap && --snap->refs == 0) {
        site_config_free(snap->cfg);
        free(snap);
    }
}

void config_store_destroy(config_store_t *store)
{
    if (!store)
        return;
    snapshot_unref(store->current);
    pthread_mutex_destroy(&store->lock);
    free(store);
}

void config_store_set(config_store_t *store, site_config_t *cfg)
{
    config_snapshot_t *snap = calloc(1, sizeof(*snap));
    if (!snap) {
        site_config_free(cfg);
        return;
    }
    snap->cfg = cfg;
    snap->refs = 1; /* referencia del store */

    pthread_mutex_lock(&store->lock);
    config_snapshot_t *old = store->current;
    store->current = snap;
    snapshot_unref(old);
    pthread_mutex_unlock(&store->lock);
}

config_snapshot_t *config_store_acquire(config_store_t *store)
{
    pthread_mutex_lock(&store->lock);
    config_snapshot_t *snap = store->current;
    if (snap)
        snap->refs++;
    pthread_mutex_unlock(&store->lock);
    return snap;
}

const site_config_t *config_snapshot_get(const config_snapshot_t *snap)
{
    return snap ? snap->cfg : NULL;
}

void config_store_release(config_store_t *store, config_snapshot_t *snap)
{
    if (!snap)
        return;
    pthread_mutex_lock(&store->lock);
    snapshot_unref(snap);
    pthread_mutex_unlock(&store->lock);
}

static char *read_file(const char *path, char *err, size_t err_size)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        snprintf(err, err_size, "no se pudo abrir %s", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *data = len >= 0 ? malloc((size_t)len + 1) : NULL;
    if (!data || fread(data, 1, (size_t)len, f) != (size_t)len) {
        snprintf(err, err_size, "no se pudo leer %s", path);
        free(data);
        fclose(f);
        return NULL;
    }
    data[len] = '\0';
    fclose(f);
    return data;
}

int config_store_refresh(config_store_t *store, const settings_t *s, char *err, size_t err_size)
{
    char *json = s->config_file[0]
                     ? read_file(s->config_file, err, err_size)
                     : origin_get_text(s->config_url, s->config_token, s->origin_timeout_seconds,
                                       err, err_size);
    if (!json)
        return -1;
    site_config_t *cfg = site_config_parse(json, err, err_size);
    free(json);
    if (!cfg)
        return -1;
    config_store_set(store, cfg);
    return 0;
}
