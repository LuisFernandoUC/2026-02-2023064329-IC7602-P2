/*
 * policy.h - Interfaz de la librería de algoritmos de reemplazo.
 *
 * La caché no conoce cómo se elige la víctima: solo notifica los eventos
 * (inserción, acceso, eliminación) y pide una víctima cuando necesita espacio.
 * Interfaz acordada en T-13 (policy_on_access, policy_on_insert, policy_pick_victim).
 *
 * Algoritmos soportados:
 *   LRU    - Least Recently Used: sale la entrada usada hace más tiempo.
 *   LFU    - Least Frequently Used: sale la de menor cantidad de accesos
 *            (empate: la usada hace más tiempo).
 *   FIFO   - First-In, First-Out: sale la insertada primero.
 *   MRU    - Most Recently Used: sale la usada más recientemente.
 *   RANDOM - sale una entrada elegida al azar.
 */
#ifndef ZC_POLICY_H
#define ZC_POLICY_H

#include <stddef.h>

typedef enum {
    POLICY_LRU = 0,
    POLICY_LFU,
    POLICY_FIFO,
    POLICY_MRU,
    POLICY_RANDOM
} policy_type_t;

typedef struct policy policy_t;

/* Crea una política vacía del tipo indicado. seed se usa solo por RANDOM. */
policy_t *policy_create(policy_type_t type, unsigned int seed);
void policy_destroy(policy_t *p);

/* Cambia el algoritmo conservando la historia de accesos de las entradas. */
void policy_set_type(policy_t *p, policy_type_t type);
policy_type_t policy_get_type(const policy_t *p);

/* Registra una nueva entrada. Si ya existía se trata como un acceso. Devuelve 0 si tuvo éxito. */
int policy_on_insert(policy_t *p, const char *key);

/* Registra un acceso (hit) a una entrada existente. */
void policy_on_access(policy_t *p, const char *key);

/* Olvida una entrada (eliminada o expirada). */
void policy_on_remove(policy_t *p, const char *key);

/*
 * Devuelve la clave de la entrada que debe salir según el algoritmo, o NULL si
 * no hay entradas. El puntero es válido hasta la siguiente llamada que modifique
 * la política; no se elimina la entrada: el llamador debe invocar policy_on_remove.
 */
const char *policy_pick_victim(policy_t *p);

size_t policy_count(const policy_t *p);

/* "LRU", "lfu", "Fifo"... -> tipo. Devuelve 0 si la cadena es válida, -1 si no. */
int policy_from_string(const char *name, policy_type_t *out);
const char *policy_to_string(policy_type_t type);

#endif
