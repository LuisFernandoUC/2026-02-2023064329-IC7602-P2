#ifndef DOMAINS_H
#define DOMAINS_H

#include <pthread.h>
#include <stddef.h>

#include "config.h"

#define DYN_MAX_DOMAINS 256

/* Lista de dominios obtenida del API (ya normalizada: minúsculas, sin punto final, sin repetidos). */
typedef struct {
    char names[DYN_MAX_DOMAINS][CFG_NAME_LEN];
    size_t count;
} domain_list_t;

/* Interpreta la respuesta de GET /api/cache/config: {"domains":[{"name":"ejemplo.com",...},...]}.
 * Solo se lee el campo "name" de cada dominio; lo demás se ignora. Los nombres inválidos se
 * descartan. Devuelve 0 si el JSON es válido y trae el arreglo "domains", y -1 si no. */
int domains_parse_json(const char *json, domain_list_t *out);

/* Dominios dinámicos: los reemplaza el hilo de sincronización y los consultan los hilos de consulta. */
typedef struct {
    pthread_mutex_t lock;
    domain_list_t list;
} dyn_domains_t;

void dyn_domains_init(dyn_domains_t *d);

/* Sustituye la lista completa. Devuelve 1 si cambió y 0 si era igual. */
int dyn_domains_replace(dyn_domains_t *d, const domain_list_t *list);

size_t dyn_domains_count(dyn_domains_t *d);

/* 1 si name es uno de los dominios o un subdominio suyo (www.ejemplo.com para ejemplo.com). */
int dyn_domains_match(dyn_domains_t *d, const char *name);

#endif
