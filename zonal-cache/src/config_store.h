/*
 * config_store.h - Configuración vigente de dominios, compartida entre hilos.
 *
 * La configuración se reemplaza completa en cada refresco. Los hilos que atienden
 * peticiones toman una referencia (acquire) y la sueltan al terminar (release), así
 * un refresco nunca libera una configuración que todavía se está usando.
 */
#ifndef ZC_CONFIG_STORE_H
#define ZC_CONFIG_STORE_H

#include <stddef.h>

#include "settings.h"
#include "site_config.h"

typedef struct config_store config_store_t;
typedef struct config_snapshot config_snapshot_t;

config_store_t *config_store_create(void);
void config_store_destroy(config_store_t *store);

/* Publica una configuración nueva (el store toma posesión de cfg). */
void config_store_set(config_store_t *store, site_config_t *cfg);

/* Toma la configuración vigente (puede ser NULL si nunca se cargó). */
config_snapshot_t *config_store_acquire(config_store_t *store);
const site_config_t *config_snapshot_get(const config_snapshot_t *snap);
void config_store_release(config_store_t *store, config_snapshot_t *snap);

/*
 * Descarga la configuración (ZC_CONFIG_FILE o ZC_CONFIG_URL), la valida y la publica.
 * Si falla, la configuración anterior se mantiene. Devuelve 0 si tuvo éxito.
 */
int config_store_refresh(config_store_t *store, const settings_t *s, char *err, size_t err_size);

#endif
