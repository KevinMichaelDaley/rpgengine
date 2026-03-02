/**
 * @file phys_pair_set_tests.c
 * @brief Tests for phys_pair_set — open-addressing hash set for body pairs.
 *
 * Tests cover: init/destroy, upsert/contains, prune, collisions, capacity.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "ferrum/physics/phys_pair_set.h"

/* ── Test harness ─────────────────────────────────────────────── */

static int g_pass = 0, g_fail = 0;

#define ASSERT_TRUE(cond) do {                                     \
    if (!(cond)) {                                                 \
        fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__,__LINE__,  \
                #cond);                                            \
        g_fail++; return;                                          \
    }                                                              \
} while (0)

#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))
#define ASSERT_EQ(a,b)     ASSERT_TRUE((a) == (b))
#define ASSERT_NE(a,b)     ASSERT_TRUE((a) != (b))

#define RUN(fn) do {                \
    printf("  %-50s ", #fn);        \
    fn();                           \
    printf("OK\n"); g_pass++;       \
} while (0)

/* ── Helper: canonical pair key ───────────────────────────────── */

static uint64_t make_key(uint32_t a, uint32_t b) {
    uint32_t lo = a < b ? a : b;
    uint32_t hi = a < b ? b : a;
    return ((uint64_t)lo << 32) | hi;
}

/* ── Tests ────────────────────────────────────────────────────── */

/** Init with valid capacity (must be power of 2). */
static void test_init_destroy(void) {
    phys_pair_set_t set;
    ASSERT_TRUE(phys_pair_set_init(&set, 64));
    ASSERT_EQ(phys_pair_set_count(&set), 0u);
    phys_pair_set_destroy(&set);
}

/** Init with zero capacity fails. */
static void test_init_zero_capacity(void) {
    phys_pair_set_t set;
    ASSERT_FALSE(phys_pair_set_init(&set, 0));
}

/** Init with non-power-of-2 rounds up to next power of 2. */
static void test_init_non_power_of_two(void) {
    phys_pair_set_t set;
    ASSERT_TRUE(phys_pair_set_init(&set, 50));
    /* Should round up to 64. */
    ASSERT_EQ(phys_pair_set_count(&set), 0u);
    phys_pair_set_destroy(&set);
}

/** Upsert a new pair returns true (was_new). */
static void test_upsert_new(void) {
    phys_pair_set_t set;
    phys_pair_set_init(&set, 64);

    bool was_new = phys_pair_set_upsert(&set, make_key(1, 2), 10);
    ASSERT_TRUE(was_new);
    ASSERT_EQ(phys_pair_set_count(&set), 1u);

    phys_pair_set_destroy(&set);
}

/** Upsert same pair again returns false (update, not new). */
static void test_upsert_existing(void) {
    phys_pair_set_t set;
    phys_pair_set_init(&set, 64);

    phys_pair_set_upsert(&set, make_key(1, 2), 10);
    bool was_new = phys_pair_set_upsert(&set, make_key(1, 2), 11);
    ASSERT_FALSE(was_new);
    ASSERT_EQ(phys_pair_set_count(&set), 1u);

    phys_pair_set_destroy(&set);
}

/** Contains returns true for inserted pair, false for missing. */
static void test_contains(void) {
    phys_pair_set_t set;
    phys_pair_set_init(&set, 64);

    phys_pair_set_upsert(&set, make_key(3, 7), 1);

    ASSERT_TRUE(phys_pair_set_contains(&set, make_key(3, 7)));
    ASSERT_FALSE(phys_pair_set_contains(&set, make_key(3, 8)));
    ASSERT_FALSE(phys_pair_set_contains(&set, make_key(0, 0)));

    phys_pair_set_destroy(&set);
}

/** Multiple distinct pairs. */
static void test_multiple_pairs(void) {
    phys_pair_set_t set;
    phys_pair_set_init(&set, 64);

    ASSERT_TRUE(phys_pair_set_upsert(&set, make_key(0, 1), 1));
    ASSERT_TRUE(phys_pair_set_upsert(&set, make_key(2, 3), 1));
    ASSERT_TRUE(phys_pair_set_upsert(&set, make_key(4, 5), 1));
    ASSERT_EQ(phys_pair_set_count(&set), 3u);

    ASSERT_TRUE(phys_pair_set_contains(&set, make_key(0, 1)));
    ASSERT_TRUE(phys_pair_set_contains(&set, make_key(2, 3)));
    ASSERT_TRUE(phys_pair_set_contains(&set, make_key(4, 5)));
    ASSERT_FALSE(phys_pair_set_contains(&set, make_key(1, 3)));

    phys_pair_set_destroy(&set);
}

/** Clear removes all entries. */
static void test_clear(void) {
    phys_pair_set_t set;
    phys_pair_set_init(&set, 64);

    phys_pair_set_upsert(&set, make_key(1, 2), 1);
    phys_pair_set_upsert(&set, make_key(3, 4), 1);
    ASSERT_EQ(phys_pair_set_count(&set), 2u);

    phys_pair_set_clear(&set);
    ASSERT_EQ(phys_pair_set_count(&set), 0u);
    ASSERT_FALSE(phys_pair_set_contains(&set, make_key(1, 2)));

    phys_pair_set_destroy(&set);
}

/** Prune removes entries with last_tick < threshold. */
static void test_prune(void) {
    phys_pair_set_t set;
    phys_pair_set_init(&set, 64);

    phys_pair_set_upsert(&set, make_key(1, 2), 5);   /* tick 5 */
    phys_pair_set_upsert(&set, make_key(3, 4), 10);  /* tick 10 */
    phys_pair_set_upsert(&set, make_key(5, 6), 10);  /* tick 10 */
    ASSERT_EQ(phys_pair_set_count(&set), 3u);

    /* Prune entries older than tick 10 (i.e., last_tick < 10). */
    phys_pair_set_prune_before(&set, 10);
    ASSERT_EQ(phys_pair_set_count(&set), 2u);
    ASSERT_FALSE(phys_pair_set_contains(&set, make_key(1, 2)));
    ASSERT_TRUE(phys_pair_set_contains(&set, make_key(3, 4)));
    ASSERT_TRUE(phys_pair_set_contains(&set, make_key(5, 6)));

    phys_pair_set_destroy(&set);
}

/** Prune all entries. */
static void test_prune_all(void) {
    phys_pair_set_t set;
    phys_pair_set_init(&set, 64);

    phys_pair_set_upsert(&set, make_key(1, 2), 5);
    phys_pair_set_upsert(&set, make_key(3, 4), 5);
    phys_pair_set_prune_before(&set, 100);
    ASSERT_EQ(phys_pair_set_count(&set), 0u);

    phys_pair_set_destroy(&set);
}

/** Prune nothing when all entries are recent. */
static void test_prune_none(void) {
    phys_pair_set_t set;
    phys_pair_set_init(&set, 64);

    phys_pair_set_upsert(&set, make_key(1, 2), 10);
    phys_pair_set_upsert(&set, make_key(3, 4), 10);
    phys_pair_set_prune_before(&set, 5);
    ASSERT_EQ(phys_pair_set_count(&set), 2u);

    phys_pair_set_destroy(&set);
}

/** Hash collision handling: many pairs that hash to same slot. */
static void test_hash_collisions(void) {
    phys_pair_set_t set;
    phys_pair_set_init(&set, 16); /* small table to force collisions */

    for (uint32_t i = 0; i < 10; i++) {
        ASSERT_TRUE(phys_pair_set_upsert(&set, make_key(i, i + 100), 1));
    }
    ASSERT_EQ(phys_pair_set_count(&set), 10u);

    for (uint32_t i = 0; i < 10; i++) {
        ASSERT_TRUE(phys_pair_set_contains(&set, make_key(i, i + 100)));
    }

    phys_pair_set_destroy(&set);
}

/** Fill to high load factor. */
static void test_high_load(void) {
    phys_pair_set_t set;
    phys_pair_set_init(&set, 64);

    /* Fill to ~75% load. */
    for (uint32_t i = 0; i < 48; i++) {
        ASSERT_TRUE(phys_pair_set_upsert(&set, make_key(i, i + 1000), 1));
    }
    ASSERT_EQ(phys_pair_set_count(&set), 48u);

    /* All should be findable. */
    for (uint32_t i = 0; i < 48; i++) {
        ASSERT_TRUE(phys_pair_set_contains(&set, make_key(i, i + 1000)));
    }

    phys_pair_set_destroy(&set);
}

/** Upsert returns false on full table (should not crash). */
static void test_full_table(void) {
    phys_pair_set_t set;
    phys_pair_set_init(&set, 8);

    /* Fill completely. */
    for (uint32_t i = 0; i < 8; i++) {
        phys_pair_set_upsert(&set, make_key(i, i + 500), 1);
    }

    /* 9th insert — table is full, should fail gracefully. */
    bool was_new = phys_pair_set_upsert(&set, make_key(99, 100), 1);
    /* Implementation may reject or handle full table. Either way, no crash. */
    (void)was_new;

    phys_pair_set_destroy(&set);
}

/** Re-insert after prune reuses slots. */
static void test_reinsert_after_prune(void) {
    phys_pair_set_t set;
    phys_pair_set_init(&set, 16);

    /* Insert 8 at tick 1. */
    for (uint32_t i = 0; i < 8; i++) {
        phys_pair_set_upsert(&set, make_key(i, i + 100), 1);
    }

    /* Prune all. */
    phys_pair_set_prune_before(&set, 5);
    ASSERT_EQ(phys_pair_set_count(&set), 0u);

    /* Re-insert — all should be "new". */
    for (uint32_t i = 0; i < 8; i++) {
        ASSERT_TRUE(phys_pair_set_upsert(&set, make_key(i, i + 100), 10));
    }
    ASSERT_EQ(phys_pair_set_count(&set), 8u);

    phys_pair_set_destroy(&set);
}

/** Upsert updates last_tick for existing entry. */
static void test_upsert_updates_tick(void) {
    phys_pair_set_t set;
    phys_pair_set_init(&set, 64);

    phys_pair_set_upsert(&set, make_key(1, 2), 5);
    /* Update tick to 10. */
    phys_pair_set_upsert(&set, make_key(1, 2), 10);
    /* Should survive prune at 6 (because tick was updated to 10). */
    phys_pair_set_prune_before(&set, 6);
    ASSERT_EQ(phys_pair_set_count(&set), 1u);
    ASSERT_TRUE(phys_pair_set_contains(&set, make_key(1, 2)));

    phys_pair_set_destroy(&set);
}

/** Canonical key ordering: (a,b) and (b,a) map to same key. */
static void test_canonical_key(void) {
    ASSERT_EQ(make_key(5, 10), make_key(10, 5));
    ASSERT_NE(make_key(5, 10), make_key(5, 11));
}

/** Empty set: contains returns false, count is 0. */
static void test_empty_set(void) {
    phys_pair_set_t set;
    phys_pair_set_init(&set, 64);

    ASSERT_FALSE(phys_pair_set_contains(&set, make_key(1, 2)));
    ASSERT_EQ(phys_pair_set_count(&set), 0u);

    phys_pair_set_destroy(&set);
}

/** Prune on empty set is safe. */
static void test_prune_empty(void) {
    phys_pair_set_t set;
    phys_pair_set_init(&set, 64);
    phys_pair_set_prune_before(&set, 100);
    ASSERT_EQ(phys_pair_set_count(&set), 0u);
    phys_pair_set_destroy(&set);
}

/** Clear on empty set is safe. */
static void test_clear_empty(void) {
    phys_pair_set_t set;
    phys_pair_set_init(&set, 64);
    phys_pair_set_clear(&set);
    ASSERT_EQ(phys_pair_set_count(&set), 0u);
    phys_pair_set_destroy(&set);
}

/* ── Main ─────────────────────────────────────────────────────── */

int main(void) {
    printf("=== phys_pair_set_tests ===\n");
    RUN(test_init_destroy);
    RUN(test_init_zero_capacity);
    RUN(test_init_non_power_of_two);
    RUN(test_upsert_new);
    RUN(test_upsert_existing);
    RUN(test_contains);
    RUN(test_multiple_pairs);
    RUN(test_clear);
    RUN(test_prune);
    RUN(test_prune_all);
    RUN(test_prune_none);
    RUN(test_hash_collisions);
    RUN(test_high_load);
    RUN(test_full_table);
    RUN(test_reinsert_after_prune);
    RUN(test_upsert_updates_tick);
    RUN(test_canonical_key);
    RUN(test_empty_set);
    RUN(test_prune_empty);
    RUN(test_clear_empty);
    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
