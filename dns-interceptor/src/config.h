#ifndef CONFIG_H
#define CONFIG_H

#include <stddef.h>
#include <stdint.h>

#define CFG_MAX_DOMAINS 64
#define CFG_MAX_ZONES 16
#define CFG_NAME_LEN 64
#define CFG_PATH_LEN 256

typedef struct {
    char name[CFG_NAME_LEN];
    uint32_t ip; /* IP de la caché zonal, en orden de host */
} zone_ip_t;

typedef struct {
    int port;
    char domains[CFG_MAX_DOMAINS][CFG_NAME_LEN]; /* "ejemplo.com" o "*.ejemplo.com" */
    size_t n_domains;
    zone_ip_t zones[CFG_MAX_ZONES];
    size_t n_zones;
    char default_zone[CFG_NAME_LEN];
    char upstream_host[CFG_NAME_LEN];
    int upstream_port;
    int upstream_timeout_ms;
    char country_db[CFG_PATH_LEN];
    char zones_file[CFG_PATH_LEN];
    uint32_t ttl;
} config_t;

/* Lee la configuración de las variables de entorno DI_* (ver README).
 * Devuelve 0 si es válida y -1 si no; en ese caso escribe el motivo en err. */
int config_from_env(config_t *cfg, char *err, size_t errlen);

/* 1 si el dominio debe ser interceptado (coincidencia exacta o comodín "*.dominio"). */
int config_domain_match(const config_t *cfg, const char *name);

/* Obtiene la IP de una zona. Devuelve 0 si existe y -1 si no. */
int config_zone_ip(const config_t *cfg, const char *zone, uint32_t *ip);

#endif
