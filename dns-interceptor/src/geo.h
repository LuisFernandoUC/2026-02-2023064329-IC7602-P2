#ifndef GEO_H
#define GEO_H

#include <stddef.h>
#include <stdint.h>

/* Base de datos "IP to Country" (db-ip lite, CSV con start_ip,end_ip,country_code). */
typedef struct geo_db geo_db_t;

/* Tabla país -> zona de la caché zonal (CSV con country,zone). */
typedef struct zone_map zone_map_t;

/* Convierte "a.b.c.d" a un entero en orden de host. Devuelve 0 si es válida y -1 si no. */
int geo_parse_ipv4(const char *text, uint32_t *ip);

/* Carga solo los rangos IPv4 del CSV. Devuelve NULL si el archivo no se puede leer. */
geo_db_t *geo_load(const char *path);
void geo_free(geo_db_t *db);
size_t geo_size(const geo_db_t *db);

/* Busca el país de una IP (búsqueda binaria). Devuelve 0 y escribe el código en out,
 * o -1 si la IP no está en la base. */
int geo_country(const geo_db_t *db, uint32_t ip, char out[3]);

zone_map_t *zones_load(const char *path);
void zones_free(zone_map_t *map);

/* Devuelve la zona de un país, o NULL si no está en la tabla. */
const char *zones_lookup(const zone_map_t *map, const char *country);

#endif
