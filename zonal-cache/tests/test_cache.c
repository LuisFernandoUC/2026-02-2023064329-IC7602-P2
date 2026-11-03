#include "test.h"
#include "../src/cache.h"

#include <dirent.h>
#include <stdlib.h>
#include <sys/stat.h>

/* Tamaño en disco de un objeto con cuerpo de body_len bytes y la metadata usada abajo. */
static size_t object_size(zc_cache_t *c, size_t body_len)
{
    cache_scope_t s = {"probe", 1 << 20, "/*", 1 << 20, POLICY_LRU};
    char *body = calloc(1, body_len + 1);
    cache_object_t o = {.status = 200, .content_type = "text/plain", .body = (unsigned char *)body,
                        .body_len = body_len, .stored_at = 100, .expires_at = 200};
    cache_put(c, &s, "probe", &o);
    size_t size = cache_partition_usage(c, "probe", "/*");
    cache_remove(c, &s, "probe");
    free(body);
    return size;
}

static cache_put_result_t put(zc_cache_t *c, const cache_scope_t *s, const char *key,
                              const char *body, time_t expires)
{
    cache_object_t o = {.status = 200, .content_type = "text/plain",
                        .body = (unsigned char *)body, .body_len = strlen(body),
                        .stored_at = 100, .expires_at = expires};
    return cache_put(c, s, key, &o);
}

static int count_files(const char *dir)
{
    int n = 0;
    DIR *d = opendir(dir);
    if (!d)
        return 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.')
            continue;
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        struct stat st;
        if (stat(path, &st) == 0 && S_ISDIR(st.st_mode))
            n += count_files(path);
        else
            n++;
    }
    closedir(d);
    return n;
}

TEST(put_then_get_roundtrip)
{
    char dir[256];
    test_tmpdir(dir, sizeof(dir));
    zc_cache_t *c = cache_create(dir, 1);
    cache_scope_t s = {"a.com", 10000, "/*", 10000, POLICY_LRU};

    CHECK_EQ_INT(put(c, &s, "a.com/x", "contenido", 500), CACHE_STORED);
    cache_object_t o;
    CHECK_EQ_INT(cache_get(c, &s, "a.com/x", 150, &o), 1);
    CHECK_EQ_INT(o.status, 200);
    CHECK_EQ_STR(o.content_type, "text/plain");
    CHECK_EQ_INT(o.body_len, 9);
    CHECK(memcmp(o.body, "contenido", 9) == 0);
    CHECK_EQ_INT(o.stored_at, 100);
    CHECK_EQ_INT(o.expires_at, 500);
    cache_object_free(&o);
    CHECK_EQ_INT(count_files(dir), 1);
    CHECK_EQ_INT(cache_get(c, &s, "a.com/otra", 150, &o), 0);
    cache_destroy(c);
}

TEST(binary_body_is_preserved)
{
    char dir[256];
    test_tmpdir(dir, sizeof(dir));
    zc_cache_t *c = cache_create(dir, 1);
    cache_scope_t s = {"a.com", 10000, "/*", 10000, POLICY_LRU};
    unsigned char bin[256];
    for (int i = 0; i < 256; i++)
        bin[i] = (unsigned char)i;
    cache_object_t in = {.status = 200, .content_type = "image/png", .body = bin,
                         .body_len = sizeof(bin), .stored_at = 1, .expires_at = 1000};
    CHECK_EQ_INT(cache_put(c, &s, "k", &in), CACHE_STORED);
    cache_object_t out;
    CHECK_EQ_INT(cache_get(c, &s, "k", 2, &out), 1);
    CHECK_EQ_INT(out.body_len, 256);
    CHECK(memcmp(out.body, bin, 256) == 0);
    cache_object_free(&out);
    cache_destroy(c);
}

TEST(expired_entries_are_removed)
{
    char dir[256];
    test_tmpdir(dir, sizeof(dir));
    zc_cache_t *c = cache_create(dir, 1);
    cache_scope_t s = {"a.com", 10000, "/*", 10000, POLICY_LRU};
    put(c, &s, "k", "x", 200);
    cache_object_t o;
    CHECK_EQ_INT(cache_get(c, &s, "k", 200, &o), 0); /* expires_at <= now */
    CHECK(!cache_contains(c, "a.com", "/*", "k"));
    CHECK_EQ_INT(cache_domain_usage(c, "a.com"), 0);
    CHECK_EQ_INT(count_files(dir), 0);
    cache_destroy(c);
}

TEST(replacing_key_updates_usage)
{
    char dir[256];
    test_tmpdir(dir, sizeof(dir));
    zc_cache_t *c = cache_create(dir, 1);
    cache_scope_t s = {"a.com", 10000, "/*", 10000, POLICY_LRU};
    put(c, &s, "k", "corto", 500);
    size_t first = cache_partition_usage(c, "a.com", "/*");
    put(c, &s, "k", "mucho mas largo", 500);
    CHECK_EQ_INT(cache_partition_count(c, "a.com", "/*"), 1);
    /* +10 bytes de cuerpo y +1 dígito en la línea de longitud ("5" -> "15") */
    CHECK_EQ_INT(cache_partition_usage(c, "a.com", "/*"), first + 11);
    CHECK_EQ_INT(count_files(dir), 1);
    cache_destroy(c);
}

TEST(partition_limit_evicts_with_lru)
{
    char dir[256];
    test_tmpdir(dir, sizeof(dir));
    zc_cache_t *c = cache_create(dir, 1);
    size_t one = object_size(c, 4);
    cache_scope_t s = {"a.com", 100000, "/*", one * 3, POLICY_LRU};
    put(c, &s, "k1", "1111", 500);
    put(c, &s, "k2", "2222", 500);
    put(c, &s, "k3", "3333", 500);
    cache_object_t o;
    CHECK_EQ_INT(cache_get(c, &s, "k1", 150, &o), 1); /* k1 pasa a ser el más reciente */
    cache_object_free(&o);
    put(c, &s, "k4", "4444", 500);
    CHECK(cache_contains(c, "a.com", "/*", "k1"));
    CHECK(!cache_contains(c, "a.com", "/*", "k2"));
    CHECK(cache_contains(c, "a.com", "/*", "k3"));
    CHECK(cache_contains(c, "a.com", "/*", "k4"));
    CHECK(cache_partition_usage(c, "a.com", "/*") <= one * 3);
    CHECK_EQ_INT(count_files(dir), 3);
    cache_destroy(c);
}

TEST(each_policy_evicts_expected_key)
{
    /*
     * Se insertan k1..k4 y se accede en el orden k2 k2 k2 k1 k1 k3 k4 k4:
     *   accesos totales: k1=3 k2=4 k3=2 k4=3
     *   último uso:      k2 < k1 < k3 < k4
     * Así cada algoritmo elige una víctima distinta.
     */
    struct {
        policy_type_t policy;
        const char *evicted;
    } cases[] = {
        {POLICY_FIFO, "k1"}, {POLICY_LRU, "k2"}, {POLICY_LFU, "k3"}, {POLICY_MRU, "k4"},
    };
    const char *keys[] = {"k1", "k2", "k3", "k4"};
    const char *accesses[] = {"k2", "k2", "k2", "k1", "k1", "k3", "k4", "k4"};
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        char dir[256];
        test_tmpdir(dir, sizeof(dir));
        zc_cache_t *c = cache_create(dir, 1);
        size_t one = object_size(c, 4);
        cache_scope_t s = {"a.com", 100000, "/*", one * 4, cases[i].policy};
        for (size_t k = 0; k < 4; k++)
            put(c, &s, keys[k], "xxxx", 500);
        for (size_t k = 0; k < sizeof(accesses) / sizeof(accesses[0]); k++) {
            cache_object_t o;
            CHECK_EQ_INT(cache_get(c, &s, accesses[k], 150, &o), 1);
            cache_object_free(&o);
        }
        put(c, &s, "k5", "5555", 500);
        for (size_t k = 0; k < 4; k++) {
            int expected_present = strcmp(keys[k], cases[i].evicted) != 0;
            if (cache_contains(c, "a.com", "/*", keys[k]) != expected_present) {
                fprintf(stderr, "    política %s: %s %s\n", policy_to_string(cases[i].policy),
                        keys[k], expected_present ? "no debía salir" : "debía salir");
                test_current_failed = 1;
            }
        }
        CHECK(cache_contains(c, "a.com", "/*", "k5"));
        CHECK_EQ_INT(cache_partition_count(c, "a.com", "/*"), 4);
        cache_destroy(c);
    }
}

TEST(random_policy_keeps_limit)
{
    char dir[256];
    test_tmpdir(dir, sizeof(dir));
    zc_cache_t *c = cache_create(dir, 7);
    size_t one = object_size(c, 4);
    cache_scope_t s = {"a.com", 100000, "/*", one * 5, POLICY_RANDOM};
    char key[16];
    for (int i = 0; i < 50; i++) {
        snprintf(key, sizeof(key), "k%02d", i);
        CHECK_EQ_INT(put(c, &s, key, "xxxx", 500), CACHE_STORED);
    }
    CHECK_EQ_INT(cache_partition_count(c, "a.com", "/*"), 5);
    CHECK(cache_contains(c, "a.com", "/*", "k49")); /* el recién insertado siempre queda */
    CHECK_EQ_INT(count_files(dir), 5);
    cache_destroy(c);
}

TEST(domain_limit_spans_partitions)
{
    char dir[256];
    test_tmpdir(dir, sizeof(dir));
    zc_cache_t *c = cache_create(dir, 1);
    size_t one = object_size(c, 4);
    cache_scope_t img = {"a.com", one * 3, "/img/*", one * 10, POLICY_FIFO};
    cache_scope_t css = {"a.com", one * 3, "/css/*", one * 10, POLICY_FIFO};
    put(c, &img, "i1", "1111", 500);
    put(c, &img, "i2", "2222", 500);
    put(c, &img, "i3", "3333", 500);
    /* La partición css está vacía: se libera espacio en la partición más grande (img). */
    CHECK_EQ_INT(put(c, &css, "c1", "cccc", 500), CACHE_STORED);
    CHECK(!cache_contains(c, "a.com", "/img/*", "i1"));
    CHECK(cache_contains(c, "a.com", "/css/*", "c1"));
    CHECK(cache_domain_usage(c, "a.com") <= one * 3);
    /* Ahora css tiene algo: se expulsa de la propia partición. */
    CHECK_EQ_INT(put(c, &css, "c2", "cccc", 500), CACHE_STORED);
    CHECK(!cache_contains(c, "a.com", "/css/*", "c1"));
    CHECK(cache_contains(c, "a.com", "/img/*", "i2"));
    cache_destroy(c);
}

TEST(domains_are_independent)
{
    char dir[256];
    test_tmpdir(dir, sizeof(dir));
    zc_cache_t *c = cache_create(dir, 1);
    size_t one = object_size(c, 4);
    cache_scope_t a = {"a.com", one, "/*", one, POLICY_LRU};
    cache_scope_t b = {"b.com", one, "/*", one, POLICY_LRU};
    put(c, &a, "k", "aaaa", 500);
    put(c, &b, "k", "bbbb", 500);
    cache_object_t o;
    CHECK_EQ_INT(cache_get(c, &a, "k", 150, &o), 1);
    CHECK(o.body && memcmp(o.body, "aaaa", 4) == 0);
    cache_object_free(&o);
    CHECK_EQ_INT(cache_get(c, &b, "k", 150, &o), 1);
    CHECK(o.body && memcmp(o.body, "bbbb", 4) == 0);
    cache_object_free(&o);
    cache_destroy(c);
}

TEST(object_larger_than_limit_is_not_stored)
{
    char dir[256];
    test_tmpdir(dir, sizeof(dir));
    zc_cache_t *c = cache_create(dir, 1);
    cache_scope_t s = {"a.com", 10000, "/*", 50, POLICY_LRU};
    put(c, &s, "k1", "a", 500);
    char big[200];
    memset(big, 'x', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    CHECK_EQ_INT(put(c, &s, "k2", big, 500), CACHE_TOO_LARGE);
    CHECK(cache_contains(c, "a.com", "/*", "k1")); /* no se vació la caché por nada */
    CHECK(!cache_contains(c, "a.com", "/*", "k2"));
    cache_scope_t small_domain = {"a.com", 10, "/*", 10000, POLICY_LRU};
    CHECK_EQ_INT(put(c, &small_domain, "k3", "abc", 500), CACHE_TOO_LARGE);
    cache_destroy(c);
}

TEST(shrinking_limit_from_config_evicts_on_access)
{
    char dir[256];
    test_tmpdir(dir, sizeof(dir));
    zc_cache_t *c = cache_create(dir, 1);
    size_t one = object_size(c, 4);
    cache_scope_t s = {"a.com", one * 10, "/*", one * 4, POLICY_FIFO};
    put(c, &s, "k1", "1111", 500);
    put(c, &s, "k2", "2222", 500);
    put(c, &s, "k3", "3333", 500);
    s.partition_max = one; /* la UI redujo el tamaño de caché de la URL */
    cache_object_t o;
    CHECK_EQ_INT(cache_get(c, &s, "k3", 150, &o), 1);
    cache_object_free(&o);
    CHECK_EQ_INT(cache_partition_count(c, "a.com", "/*"), 1);
    CHECK(cache_contains(c, "a.com", "/*", "k3"));
    cache_destroy(c);
}

TEST(missing_file_is_treated_as_miss)
{
    char dir[256];
    test_tmpdir(dir, sizeof(dir));
    zc_cache_t *c = cache_create(dir, 1);
    cache_scope_t s = {"a.com", 10000, "/*", 10000, POLICY_LRU};
    put(c, &s, "k", "x", 500);
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "find '%s' -name '*.obj' -delete", dir);
    CHECK_EQ_INT(system(cmd), 0);
    cache_object_t o;
    CHECK_EQ_INT(cache_get(c, &s, "k", 150, &o), -1);
    CHECK(!cache_contains(c, "a.com", "/*", "k"));
    CHECK_EQ_INT(cache_domain_usage(c, "a.com"), 0);
    cache_destroy(c);
}

TEST(create_clears_previous_content)
{
    char dir[256], file[300];
    test_tmpdir(dir, sizeof(dir));
    snprintf(file, sizeof(file), "%s/viejo.obj", dir);
    FILE *f = fopen(file, "w");
    fputs("basura", f);
    fclose(f);
    zc_cache_t *c = cache_create(dir, 1);
    CHECK_EQ_INT(count_files(dir), 0);
    cache_destroy(c);
}

void suite_cache(void)
{
    RUN(put_then_get_roundtrip);
    RUN(binary_body_is_preserved);
    RUN(expired_entries_are_removed);
    RUN(replacing_key_updates_usage);
    RUN(partition_limit_evicts_with_lru);
    RUN(each_policy_evicts_expected_key);
    RUN(random_policy_keeps_limit);
    RUN(domain_limit_spans_partitions);
    RUN(domains_are_independent);
    RUN(object_larger_than_limit_is_not_stored);
    RUN(shrinking_limit_from_config_evicts_on_access);
    RUN(missing_file_is_treated_as_miss);
    RUN(create_clears_previous_content);
}
