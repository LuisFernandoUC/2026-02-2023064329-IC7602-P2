/*
 * policy.c - Algoritmos de reemplazo LRU, LFU, FIFO, MRU y Random.
 *
 * Cada entrada guarda tres contadores:
 *   inserted_at  - número de secuencia del momento en que se insertó (FIFO).
 *   last_used_at - número de secuencia del último acceso (LRU / MRU).
 *   hits         - cantidad de accesos (LFU).
 * La víctima se busca recorriendo las entradas (O(n)). Para el tamaño de las
 * cachés del proyecto esto es suficiente y permite cambiar de algoritmo en
 * caliente sin reconstruir estructuras.
 */
#include "policy.h"
#include "util.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *key;
    uint64_t inserted_at;
    uint64_t last_used_at;
    uint64_t hits;
} policy_entry_t;

struct policy {
    policy_type_t type;
    policy_entry_t *entries;
    size_t count;
    size_t capacity;
    uint64_t clock;
    unsigned int rand_state;
};

policy_t *policy_create(policy_type_t type, unsigned int seed)
{
    policy_t *p = calloc(1, sizeof(*p));
    if (!p)
        return NULL;
    p->type = type;
    p->rand_state = seed ? seed : 1u;
    return p;
}

void policy_destroy(policy_t *p)
{
    if (!p)
        return;
    for (size_t i = 0; i < p->count; i++)
        free(p->entries[i].key);
    free(p->entries);
    free(p);
}

void policy_set_type(policy_t *p, policy_type_t type)
{
    p->type = type;
}

policy_type_t policy_get_type(const policy_t *p)
{
    return p->type;
}

size_t policy_count(const policy_t *p)
{
    return p->count;
}

static long find_index(const policy_t *p, const char *key)
{
    for (size_t i = 0; i < p->count; i++)
        if (strcmp(p->entries[i].key, key) == 0)
            return (long)i;
    return -1;
}

int policy_on_insert(policy_t *p, const char *key)
{
    if (find_index(p, key) >= 0) {
        policy_on_access(p, key);
        return 0;
    }
    if (p->count == p->capacity) {
        size_t new_cap = p->capacity ? p->capacity * 2 : 16;
        policy_entry_t *grown = realloc(p->entries, new_cap * sizeof(*grown));
        if (!grown)
            return -1;
        p->entries = grown;
        p->capacity = new_cap;
    }
    char *copy = zc_strndup(key, strlen(key));
    if (!copy)
        return -1;
    p->clock++;
    policy_entry_t *e = &p->entries[p->count++];
    e->key = copy;
    e->inserted_at = p->clock;
    e->last_used_at = p->clock;
    e->hits = 1;
    return 0;
}

void policy_on_access(policy_t *p, const char *key)
{
    long i = find_index(p, key);
    if (i < 0)
        return;
    p->clock++;
    p->entries[i].last_used_at = p->clock;
    p->entries[i].hits++;
}

void policy_on_remove(policy_t *p, const char *key)
{
    long i = find_index(p, key);
    if (i < 0)
        return;
    free(p->entries[i].key);
    p->entries[i] = p->entries[p->count - 1];
    p->count--;
}

/* Generador lineal congruente propio para no depender del estado global de rand(). */
static unsigned int next_random(policy_t *p)
{
    p->rand_state = p->rand_state * 1103515245u + 12345u;
    return (p->rand_state >> 16) & 0x7fff;
}

/* Devuelve 1 si a debe salir antes que b según el algoritmo activo. */
static int is_better_victim(policy_type_t type, const policy_entry_t *a, const policy_entry_t *b)
{
    switch (type) {
    case POLICY_LRU:
        return a->last_used_at < b->last_used_at;
    case POLICY_MRU:
        return a->last_used_at > b->last_used_at;
    case POLICY_FIFO:
        return a->inserted_at < b->inserted_at;
    case POLICY_LFU:
        if (a->hits != b->hits)
            return a->hits < b->hits;
        return a->last_used_at < b->last_used_at;
    default:
        return 0;
    }
}

const char *policy_pick_victim(policy_t *p)
{
    if (p->count == 0)
        return NULL;
    if (p->type == POLICY_RANDOM)
        return p->entries[next_random(p) % p->count].key;

    size_t best = 0;
    for (size_t i = 1; i < p->count; i++)
        if (is_better_victim(p->type, &p->entries[i], &p->entries[best]))
            best = i;
    return p->entries[best].key;
}

int policy_from_string(const char *name, policy_type_t *out)
{
    static const struct {
        const char *name;
        policy_type_t type;
    } table[] = {
        {"LRU", POLICY_LRU}, {"LFU", POLICY_LFU}, {"FIFO", POLICY_FIFO},
        {"MRU", POLICY_MRU}, {"RANDOM", POLICY_RANDOM},
    };
    if (!name)
        return -1;
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        if (zc_strcaseeq(name, table[i].name)) {
            *out = table[i].type;
            return 0;
        }
    }
    return -1;
}

const char *policy_to_string(policy_type_t type)
{
    switch (type) {
    case POLICY_LRU:
        return "LRU";
    case POLICY_LFU:
        return "LFU";
    case POLICY_FIFO:
        return "FIFO";
    case POLICY_MRU:
        return "MRU";
    case POLICY_RANDOM:
        return "RANDOM";
    }
    return "UNKNOWN";
}
