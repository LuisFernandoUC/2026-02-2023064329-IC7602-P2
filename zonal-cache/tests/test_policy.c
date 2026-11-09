#include "test.h"
#include "../src/policy.h"

/* Inserta a, b, c (en ese orden) y luego accede según accesses. */
static policy_t *setup(policy_type_t type, const char *accesses[], size_t n)
{
    policy_t *p = policy_create(type, 42);
    policy_on_insert(p, "a");
    policy_on_insert(p, "b");
    policy_on_insert(p, "c");
    for (size_t i = 0; i < n; i++)
        policy_on_access(p, accesses[i]);
    return p;
}

TEST(lru_evicts_least_recently_used)
{
    const char *acc[] = {"a", "c"};
    policy_t *p = setup(POLICY_LRU, acc, 2);
    CHECK_EQ_STR(policy_pick_victim(p), "b");
    policy_on_remove(p, "b");
    CHECK_EQ_STR(policy_pick_victim(p), "a");
    policy_destroy(p);
}

TEST(mru_evicts_most_recently_used)
{
    const char *acc[] = {"c", "a"};
    policy_t *p = setup(POLICY_MRU, acc, 2);
    CHECK_EQ_STR(policy_pick_victim(p), "a");
    policy_destroy(p);
}

TEST(mru_without_accesses_evicts_last_inserted)
{
    policy_t *p = setup(POLICY_MRU, NULL, 0);
    CHECK_EQ_STR(policy_pick_victim(p), "c");
    policy_destroy(p);
}

TEST(fifo_ignores_accesses)
{
    const char *acc[] = {"a", "a", "a"};
    policy_t *p = setup(POLICY_FIFO, acc, 3);
    CHECK_EQ_STR(policy_pick_victim(p), "a");
    policy_on_remove(p, "a");
    CHECK_EQ_STR(policy_pick_victim(p), "b");
    policy_destroy(p);
}

TEST(lfu_evicts_least_frequently_used)
{
    const char *acc[] = {"a", "a", "b", "c", "c", "c"};
    policy_t *p = setup(POLICY_LFU, acc, 6);
    CHECK_EQ_STR(policy_pick_victim(p), "b");
    policy_destroy(p);
}

TEST(lfu_tie_breaks_by_recency)
{
    const char *acc[] = {"b", "a"}; /* a y b con 2 accesos; c con 1 */
    policy_t *p = setup(POLICY_LFU, acc, 2);
    CHECK_EQ_STR(policy_pick_victim(p), "c");
    policy_on_remove(p, "c");
    CHECK_EQ_STR(policy_pick_victim(p), "b"); /* empate: b se usó antes que a */
    policy_destroy(p);
}

TEST(random_returns_existing_key_and_is_reproducible)
{
    policy_t *p1 = setup(POLICY_RANDOM, NULL, 0);
    policy_t *p2 = setup(POLICY_RANDOM, NULL, 0);
    for (int i = 0; i < 20; i++) {
        const char *v1 = policy_pick_victim(p1);
        const char *v2 = policy_pick_victim(p2);
        CHECK(v1 && (strcmp(v1, "a") == 0 || strcmp(v1, "b") == 0 || strcmp(v1, "c") == 0));
        CHECK_EQ_STR(v1, v2); /* misma semilla -> misma secuencia */
    }
    policy_destroy(p1);
    policy_destroy(p2);
}

TEST(random_eventually_picks_every_key)
{
    policy_t *p = setup(POLICY_RANDOM, NULL, 0);
    int seen_a = 0, seen_b = 0, seen_c = 0;
    for (int i = 0; i < 200; i++) {
        const char *v = policy_pick_victim(p);
        seen_a |= strcmp(v, "a") == 0;
        seen_b |= strcmp(v, "b") == 0;
        seen_c |= strcmp(v, "c") == 0;
    }
    CHECK(seen_a && seen_b && seen_c);
    policy_destroy(p);
}

TEST(empty_policy_has_no_victim)
{
    policy_type_t types[] = {POLICY_LRU, POLICY_LFU, POLICY_FIFO, POLICY_MRU, POLICY_RANDOM};
    for (size_t i = 0; i < 5; i++) {
        policy_t *p = policy_create(types[i], 1);
        CHECK(policy_pick_victim(p) == NULL);
        policy_destroy(p);
    }
}

TEST(reinsert_counts_as_access)
{
    policy_t *p = setup(POLICY_LRU, NULL, 0);
    policy_on_insert(p, "a");
    CHECK_EQ_INT(policy_count(p), 3);
    CHECK_EQ_STR(policy_pick_victim(p), "b");
    policy_destroy(p);
}

TEST(remove_and_unknown_keys)
{
    policy_t *p = setup(POLICY_LRU, NULL, 0);
    policy_on_remove(p, "zzz");
    policy_on_access(p, "zzz");
    CHECK_EQ_INT(policy_count(p), 3);
    policy_on_remove(p, "a");
    policy_on_remove(p, "b");
    policy_on_remove(p, "c");
    CHECK_EQ_INT(policy_count(p), 0);
    CHECK(policy_pick_victim(p) == NULL);
    policy_destroy(p);
}

TEST(switching_policy_keeps_history)
{
    const char *acc[] = {"a"};
    policy_t *p = setup(POLICY_FIFO, acc, 1);
    CHECK_EQ_STR(policy_pick_victim(p), "a");
    policy_set_type(p, POLICY_LRU);
    CHECK_EQ_STR(policy_pick_victim(p), "b");
    policy_set_type(p, POLICY_MRU);
    CHECK_EQ_STR(policy_pick_victim(p), "a");
    policy_destroy(p);
}

TEST(many_entries_grow_storage)
{
    policy_t *p = policy_create(POLICY_FIFO, 1);
    char key[16];
    for (int i = 0; i < 1000; i++) {
        snprintf(key, sizeof(key), "k%d", i);
        CHECK_EQ_INT(policy_on_insert(p, key), 0);
    }
    CHECK_EQ_INT(policy_count(p), 1000);
    CHECK_EQ_STR(policy_pick_victim(p), "k0");
    policy_destroy(p);
}

TEST(policy_names)
{
    policy_type_t t;
    CHECK_EQ_INT(policy_from_string("lru", &t), 0);
    CHECK_EQ_INT(t, POLICY_LRU);
    CHECK_EQ_INT(policy_from_string("Random", &t), 0);
    CHECK_EQ_INT(t, POLICY_RANDOM);
    CHECK_EQ_INT(policy_from_string("LFU", &t), 0);
    CHECK_EQ_INT(t, POLICY_LFU);
    CHECK_EQ_INT(policy_from_string("ARC", &t), -1);
    CHECK_EQ_INT(policy_from_string(NULL, &t), -1);
    CHECK_EQ_STR(policy_to_string(POLICY_MRU), "MRU");
    CHECK_EQ_STR(policy_to_string(POLICY_FIFO), "FIFO");
}

void suite_policy(void)
{
    RUN(lru_evicts_least_recently_used);
    RUN(mru_evicts_most_recently_used);
    RUN(mru_without_accesses_evicts_last_inserted);
    RUN(fifo_ignores_accesses);
    RUN(lfu_evicts_least_frequently_used);
    RUN(lfu_tie_breaks_by_recency);
    RUN(random_returns_existing_key_and_is_reproducible);
    RUN(random_eventually_picks_every_key);
    RUN(empty_policy_has_no_victim);
    RUN(reinsert_counts_as_access);
    RUN(remove_and_unknown_keys);
    RUN(switching_policy_keeps_history);
    RUN(many_entries_grow_storage);
    RUN(policy_names);
}
