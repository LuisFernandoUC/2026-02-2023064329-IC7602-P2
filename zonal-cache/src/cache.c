/*
 * cache.c - Implementación de la caché en disco (ver cache.h).
 *
 * Formato de cada archivo de objeto:
 *   ZC1\n
 *   <status>\n
 *   <stored_at>\n
 *   <expires_at>\n
 *   <content-type>\n
 *   <body_len>\n
 *   <body (body_len bytes)>
 *
 * El tamaño contabilizado de un objeto es el tamaño total de su archivo.
 */
#include "cache.h"
#include "util.h"

#include <errno.h>
#include <ftw.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    char *key;
    char *file;
    size_t size;
    time_t expires_at;
} entry_t;

typedef struct {
    char *name;
    char *dir;
    size_t max;
    size_t used;
    policy_t *policy;
    entry_t *entries;
    size_t count;
    size_t cap;
} partition_t;

typedef struct {
    char *name;
    char *dir;
    size_t max;
    size_t used;
    partition_t **parts;
    size_t count;
    size_t cap;
} domain_t;

struct zc_cache {
    char *dir;
    pthread_mutex_t lock;
    domain_t **domains;
    size_t count;
    size_t cap;
    unsigned int seed;
    uint64_t next_id;
};

/* ---------- utilidades de sistema de archivos ---------- */

static int mkdir_p(const char *path)
{
    char tmp[4096];
    if (zc_strlcpy(tmp, path, sizeof(tmp)) >= sizeof(tmp))
        return -1;
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
                return -1;
            *p = '/';
        }
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
        return -1;
    return 0;
}

static int remove_cb(const char *path, const struct stat *sb, int flag, struct FTW *ftw)
{
    (void)sb;
    (void)flag;
    if (ftw->level == 0)
        return 0; /* conservar la raíz */
    return remove(path);
}

static void clear_dir(const char *dir)
{
    nftw(dir, remove_cb, 16, FTW_DEPTH | FTW_PHYS);
}

static char *hashed_child(const char *parent, const char *name)
{
    uint64_t h = zc_fnv1a64(name, strlen(name));
    size_t len = strlen(parent) + 1 + 16 + 1;
    char *out = malloc(len);
    if (out)
        snprintf(out, len, "%s/%016" PRIx64, parent, h);
    return out;
}

/* ---------- índice en memoria ---------- */

static void free_partition(partition_t *p)
{
    for (size_t i = 0; i < p->count; i++) {
        free(p->entries[i].key);
        free(p->entries[i].file);
    }
    free(p->entries);
    policy_destroy(p->policy);
    free(p->name);
    free(p->dir);
    free(p);
}

static void free_domain(domain_t *d)
{
    for (size_t i = 0; i < d->count; i++)
        free_partition(d->parts[i]);
    free(d->parts);
    free(d->name);
    free(d->dir);
    free(d);
}

static int grow(void **arr, size_t *cap, size_t count, size_t elem)
{
    if (count < *cap)
        return 0;
    size_t new_cap = *cap ? *cap * 2 : 8;
    void *n = realloc(*arr, new_cap * elem);
    if (!n)
        return -1;
    *arr = n;
    *cap = new_cap;
    return 0;
}

static domain_t *find_domain(zc_cache_t *c, const char *name, int create)
{
    for (size_t i = 0; i < c->count; i++)
        if (strcmp(c->domains[i]->name, name) == 0)
            return c->domains[i];
    if (!create || grow((void **)&c->domains, &c->cap, c->count, sizeof(domain_t *)) != 0)
        return NULL;
    domain_t *d = calloc(1, sizeof(*d));
    if (!d)
        return NULL;
    d->name = zc_strndup(name, strlen(name));
    d->dir = hashed_child(c->dir, name);
    if (!d->name || !d->dir) {
        free_domain(d);
        return NULL;
    }
    c->domains[c->count++] = d;
    return d;
}

static partition_t *find_partition(zc_cache_t *c, domain_t *d, const char *name,
                                   policy_type_t policy, int create)
{
    for (size_t i = 0; i < d->count; i++)
        if (strcmp(d->parts[i]->name, name) == 0)
            return d->parts[i];
    if (!create || grow((void **)&d->parts, &d->cap, d->count, sizeof(partition_t *)) != 0)
        return NULL;
    partition_t *p = calloc(1, sizeof(*p));
    if (!p)
        return NULL;
    p->name = zc_strndup(name, strlen(name));
    p->dir = hashed_child(d->dir, name);
    p->policy = policy_create(policy, c->seed + (unsigned int)d->count);
    if (!p->name || !p->dir || !p->policy) {
        free_partition(p);
        return NULL;
    }
    d->parts[d->count++] = p;
    return p;
}

static long find_entry(const partition_t *p, const char *key)
{
    for (size_t i = 0; i < p->count; i++)
        if (strcmp(p->entries[i].key, key) == 0)
            return (long)i;
    return -1;
}

static void remove_entry(domain_t *d, partition_t *p, size_t idx)
{
    entry_t *e = &p->entries[idx];
    unlink(e->file);
    p->used -= e->size;
    d->used -= e->size;
    policy_on_remove(p->policy, e->key);
    free(e->key);
    free(e->file);
    p->entries[idx] = p->entries[p->count - 1];
    p->count--;
}

/* Saca una víctima de la partición según su política. Devuelve 0 si sacó algo. */
static int evict_from(domain_t *d, partition_t *p)
{
    const char *victim = policy_pick_victim(p->policy);
    if (!victim)
        return -1;
    long idx = find_entry(p, victim);
    if (idx < 0) {
        policy_on_remove(p->policy, victim); /* estado inconsistente: se corrige */
        return 0;
    }
    remove_entry(d, p, (size_t)idx);
    return 0;
}

/*
 * Libera espacio en el dominio: primero en la partición preferida y, si está vacía,
 * en la partición del dominio que más espacio ocupa.
 */
static int evict_for_domain(domain_t *d, partition_t *preferred)
{
    if (preferred && preferred->count > 0)
        return evict_from(d, preferred);
    partition_t *largest = NULL;
    for (size_t i = 0; i < d->count; i++)
        if (d->parts[i]->count > 0 && (!largest || d->parts[i]->used > largest->used))
            largest = d->parts[i];
    return largest ? evict_from(d, largest) : -1;
}

/* Actualiza límites y política (pueden cambiar desde la UI) y los hace cumplir. */
static void apply_scope(domain_t *d, partition_t *p, const cache_scope_t *s, size_t incoming)
{
    d->max = s->domain_max;
    p->max = s->partition_max;
    if (policy_get_type(p->policy) != s->policy)
        policy_set_type(p->policy, s->policy);

    while (p->count > 0 && p->used + incoming > p->max)
        if (evict_from(d, p) != 0)
            break;
    while (d->used + incoming > d->max)
        if (evict_for_domain(d, p) != 0)
            break;
}

/* ---------- serialización de objetos ---------- */

static char *serialize_header(const cache_object_t *obj, size_t *len)
{
    char ctype[sizeof(obj->content_type)];
    zc_strlcpy(ctype, obj->content_type, sizeof(ctype));
    ctype[strcspn(ctype, "\r\n")] = '\0';

    char *buf = malloc(256 + sizeof(ctype));
    if (!buf)
        return NULL;
    int n = snprintf(buf, 256 + sizeof(ctype), "ZC1\n%d\n%lld\n%lld\n%s\n%zu\n", obj->status,
                     (long long)obj->stored_at, (long long)obj->expires_at, ctype, obj->body_len);
    *len = (size_t)n;
    return buf;
}

static int write_object(const char *file, const char *header, size_t header_len,
                        const cache_object_t *obj)
{
    FILE *f = fopen(file, "wb");
    if (!f)
        return -1;
    int ok = fwrite(header, 1, header_len, f) == header_len &&
             (obj->body_len == 0 || fwrite(obj->body, 1, obj->body_len, f) == obj->body_len);
    if (fclose(f) != 0)
        ok = 0;
    if (!ok)
        unlink(file);
    return ok ? 0 : -1;
}

static int read_line(FILE *f, char *buf, size_t size)
{
    if (!fgets(buf, (int)size, f))
        return -1;
    buf[strcspn(buf, "\n")] = '\0';
    return 0;
}

static int read_object(const char *file, cache_object_t *out)
{
    memset(out, 0, sizeof(*out));
    FILE *f = fopen(file, "rb");
    if (!f)
        return -1;
    char line[256];
    long long stored = 0, expires = 0;
    size_t body_len = 0;
    int ok = read_line(f, line, sizeof(line)) == 0 && strcmp(line, "ZC1") == 0 &&
             read_line(f, line, sizeof(line)) == 0 && sscanf(line, "%d", &out->status) == 1 &&
             read_line(f, line, sizeof(line)) == 0 && sscanf(line, "%lld", &stored) == 1 &&
             read_line(f, line, sizeof(line)) == 0 && sscanf(line, "%lld", &expires) == 1 &&
             read_line(f, out->content_type, sizeof(out->content_type)) == 0 &&
             read_line(f, line, sizeof(line)) == 0 && sscanf(line, "%zu", &body_len) == 1;
    if (ok && body_len > 0) {
        out->body = malloc(body_len);
        ok = out->body && fread(out->body, 1, body_len, f) == body_len;
    }
    fclose(f);
    if (!ok) {
        cache_object_free(out);
        return -1;
    }
    out->body_len = body_len;
    out->stored_at = (time_t)stored;
    out->expires_at = (time_t)expires;
    return 0;
}

/* ---------- API pública ---------- */

zc_cache_t *cache_create(const char *dir, unsigned int seed)
{
    if (mkdir_p(dir) != 0)
        return NULL;
    zc_cache_t *c = calloc(1, sizeof(*c));
    if (!c)
        return NULL;
    c->dir = zc_strndup(dir, strlen(dir));
    if (!c->dir) {
        free(c);
        return NULL;
    }
    size_t len = strlen(c->dir);
    while (len > 1 && c->dir[len - 1] == '/')
        c->dir[--len] = '\0';
    pthread_mutex_init(&c->lock, NULL);
    c->seed = seed;
    clear_dir(c->dir);
    return c;
}

void cache_destroy(zc_cache_t *c)
{
    if (!c)
        return;
    for (size_t i = 0; i < c->count; i++)
        free_domain(c->domains[i]);
    free(c->domains);
    pthread_mutex_destroy(&c->lock);
    free(c->dir);
    free(c);
}

int cache_get(zc_cache_t *c, const cache_scope_t *s, const char *key, time_t now,
              cache_object_t *out)
{
    int result = 0;
    pthread_mutex_lock(&c->lock);
    domain_t *d = find_domain(c, s->domain, 0);
    partition_t *p = d ? find_partition(c, d, s->partition, s->policy, 0) : NULL;
    if (!p)
        goto done;
    apply_scope(d, p, s, 0);

    long idx = find_entry(p, key);
    if (idx < 0)
        goto done;
    if (p->entries[idx].expires_at <= now) {
        remove_entry(d, p, (size_t)idx);
        goto done;
    }
    if (read_object(p->entries[idx].file, out) != 0) {
        remove_entry(d, p, (size_t)idx);
        result = -1;
        goto done;
    }
    policy_on_access(p->policy, key);
    result = 1;
done:
    pthread_mutex_unlock(&c->lock);
    return result;
}

cache_put_result_t cache_put(zc_cache_t *c, const cache_scope_t *s, const char *key,
                             const cache_object_t *obj)
{
    size_t header_len = 0;
    char *header = serialize_header(obj, &header_len);
    if (!header)
        return CACHE_ERROR;
    size_t size = header_len + obj->body_len;
    cache_put_result_t result = CACHE_ERROR;

    pthread_mutex_lock(&c->lock);
    domain_t *d = find_domain(c, s->domain, 1);
    partition_t *p = d ? find_partition(c, d, s->partition, s->policy, 1) : NULL;
    if (!p)
        goto done;

    long existing = find_entry(p, key);
    if (existing >= 0)
        remove_entry(d, p, (size_t)existing);

    if (size > s->partition_max || size > s->domain_max) {
        apply_scope(d, p, s, 0);
        result = CACHE_TOO_LARGE;
        goto done;
    }
    apply_scope(d, p, s, size);
    if (p->used + size > p->max || d->used + size > d->max) {
        result = CACHE_TOO_LARGE;
        goto done;
    }

    if (mkdir_p(p->dir) != 0 ||
        grow((void **)&p->entries, &p->cap, p->count, sizeof(entry_t)) != 0)
        goto done;

    uint64_t h = zc_fnv1a64(key, strlen(key));
    size_t file_len = strlen(p->dir) + 64;
    char *file = malloc(file_len);
    char *key_copy = zc_strndup(key, strlen(key));
    if (!file || !key_copy) {
        free(file);
        free(key_copy);
        goto done;
    }
    snprintf(file, file_len, "%s/%016" PRIx64 "-%" PRIu64 ".obj", p->dir, h, ++c->next_id);
    if (write_object(file, header, header_len, obj) != 0 ||
        policy_on_insert(p->policy, key) != 0) {
        unlink(file);
        free(file);
        free(key_copy);
        goto done;
    }

    entry_t *e = &p->entries[p->count++];
    e->key = key_copy;
    e->file = file;
    e->size = size;
    e->expires_at = obj->expires_at;
    p->used += size;
    d->used += size;
    result = CACHE_STORED;
done:
    pthread_mutex_unlock(&c->lock);
    free(header);
    return result;
}

int cache_remove(zc_cache_t *c, const cache_scope_t *s, const char *key)
{
    int removed = 0;
    pthread_mutex_lock(&c->lock);
    domain_t *d = find_domain(c, s->domain, 0);
    partition_t *p = d ? find_partition(c, d, s->partition, s->policy, 0) : NULL;
    long idx = p ? find_entry(p, key) : -1;
    if (idx >= 0) {
        remove_entry(d, p, (size_t)idx);
        removed = 1;
    }
    pthread_mutex_unlock(&c->lock);
    return removed;
}

size_t cache_domain_usage(zc_cache_t *c, const char *domain)
{
    pthread_mutex_lock(&c->lock);
    domain_t *d = find_domain(c, domain, 0);
    size_t used = d ? d->used : 0;
    pthread_mutex_unlock(&c->lock);
    return used;
}

size_t cache_partition_usage(zc_cache_t *c, const char *domain, const char *partition)
{
    pthread_mutex_lock(&c->lock);
    domain_t *d = find_domain(c, domain, 0);
    partition_t *p = d ? find_partition(c, d, partition, POLICY_LRU, 0) : NULL;
    size_t used = p ? p->used : 0;
    pthread_mutex_unlock(&c->lock);
    return used;
}

size_t cache_partition_count(zc_cache_t *c, const char *domain, const char *partition)
{
    pthread_mutex_lock(&c->lock);
    domain_t *d = find_domain(c, domain, 0);
    partition_t *p = d ? find_partition(c, d, partition, POLICY_LRU, 0) : NULL;
    size_t n = p ? p->count : 0;
    pthread_mutex_unlock(&c->lock);
    return n;
}

int cache_contains(zc_cache_t *c, const char *domain, const char *partition, const char *key)
{
    pthread_mutex_lock(&c->lock);
    domain_t *d = find_domain(c, domain, 0);
    partition_t *p = d ? find_partition(c, d, partition, POLICY_LRU, 0) : NULL;
    int found = p && find_entry(p, key) >= 0;
    pthread_mutex_unlock(&c->lock);
    return found;
}

void cache_object_free(cache_object_t *obj)
{
    free(obj->body);
    obj->body = NULL;
    obj->body_len = 0;
}
