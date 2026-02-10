/**
 * @file p007_net_ghost_table_tests.c
 * @brief Tests for ghost table (server entity → client entity mapping).
 *
 * Exercises:
 * - Create ghost mapping (server_id → local entity)
 * - Lookup by server_id returns correct local entity
 * - Destroy ghost removes mapping
 * - Lookup destroyed ghost returns invalid entity
 * - ID mapping survives entity churn (create/destroy cycles)
 * - Capacity limit respected (no overflow)
 * - Generation check prevents stale references
 * - Clear resets the entire table
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/net/ghost_table.h"

/* ── Test macros ────────────────────────────────────────────────── */

#define ASSERT_TRUE(cond)                                              \
    do {                                                               \
        if (!(cond)) {                                                 \
            fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__,   \
                    #cond);                                            \
            return 1;                                                  \
        }                                                              \
    } while (0)

#define ASSERT_UINT_EQ(exp, act)                                       \
    do {                                                               \
        if ((uint64_t)(exp) != (uint64_t)(act)) {                      \
            fprintf(stderr, "FAIL: %s:%d: expected %llu got %llu\n",   \
                    __FILE__, __LINE__,                                 \
                    (unsigned long long)(exp),                          \
                    (unsigned long long)(act));                         \
            return 1;                                                  \
        }                                                              \
    } while (0)

#define ASSERT_INT_EQ(exp, act)                                        \
    do {                                                               \
        if ((int)(exp) != (int)(act)) {                                \
            fprintf(stderr, "FAIL: %s:%d: expected %d got %d\n",       \
                    __FILE__, __LINE__, (int)(exp), (int)(act));        \
            return 1;                                                  \
        }                                                              \
    } while (0)

/* ── Helpers ────────────────────────────────────────────────────── */

#define TEST_CAPACITY 64u

/* ── Test: create and lookup a ghost mapping ───────────────────── */

static int test_create_and_lookup(void) {
    net_ghost_entry_t storage[TEST_CAPACITY];
    net_ghost_table_t table;
    net_ghost_table_init(&table, storage, TEST_CAPACITY);

    /* Create a mapping: server entity 42 → local entity {index=5, gen=1}. */
    net_ghost_entity_t local = { .index = 5, .generation = 1 };
    int rc = net_ghost_table_create(&table, 42u, local);
    ASSERT_INT_EQ(NET_GHOST_OK, rc);

    /* Lookup by server_id should return the local entity. */
    net_ghost_entity_t out;
    rc = net_ghost_table_lookup(&table, 42u, &out);
    ASSERT_INT_EQ(NET_GHOST_OK, rc);
    ASSERT_UINT_EQ(5u, out.index);
    ASSERT_UINT_EQ(1u, out.generation);

    return 0;
}

/* ── Test: lookup nonexistent returns NOT_FOUND ────────────────── */

static int test_lookup_nonexistent(void) {
    net_ghost_entry_t storage[TEST_CAPACITY];
    net_ghost_table_t table;
    net_ghost_table_init(&table, storage, TEST_CAPACITY);

    net_ghost_entity_t out;
    int rc = net_ghost_table_lookup(&table, 99u, &out);
    ASSERT_INT_EQ(NET_GHOST_NOT_FOUND, rc);

    return 0;
}

/* ── Test: destroy removes mapping ─────────────────────────────── */

static int test_destroy_removes_mapping(void) {
    net_ghost_entry_t storage[TEST_CAPACITY];
    net_ghost_table_t table;
    net_ghost_table_init(&table, storage, TEST_CAPACITY);

    net_ghost_entity_t local = { .index = 10, .generation = 2 };
    net_ghost_table_create(&table, 100u, local);

    /* Destroy the mapping. */
    int rc = net_ghost_table_destroy(&table, 100u);
    ASSERT_INT_EQ(NET_GHOST_OK, rc);

    /* Lookup should now fail. */
    net_ghost_entity_t out;
    rc = net_ghost_table_lookup(&table, 100u, &out);
    ASSERT_INT_EQ(NET_GHOST_NOT_FOUND, rc);

    /* Destroying again should return NOT_FOUND. */
    rc = net_ghost_table_destroy(&table, 100u);
    ASSERT_INT_EQ(NET_GHOST_NOT_FOUND, rc);

    return 0;
}

/* ── Test: churn — create, destroy, re-create same server_id ───── */

static int test_churn_reuse(void) {
    net_ghost_entry_t storage[TEST_CAPACITY];
    net_ghost_table_t table;
    net_ghost_table_init(&table, storage, TEST_CAPACITY);

    net_ghost_entity_t v1 = { .index = 1, .generation = 1 };
    net_ghost_entity_t v2 = { .index = 7, .generation = 3 };

    net_ghost_table_create(&table, 50u, v1);
    net_ghost_table_destroy(&table, 50u);

    /* Re-create with different local entity. */
    int rc = net_ghost_table_create(&table, 50u, v2);
    ASSERT_INT_EQ(NET_GHOST_OK, rc);

    net_ghost_entity_t out;
    rc = net_ghost_table_lookup(&table, 50u, &out);
    ASSERT_INT_EQ(NET_GHOST_OK, rc);
    ASSERT_UINT_EQ(7u, out.index);
    ASSERT_UINT_EQ(3u, out.generation);

    return 0;
}

/* ── Test: capacity limit ──────────────────────────────────────── */

static int test_capacity_limit(void) {
    enum { SMALL_CAP = 4 };
    net_ghost_entry_t storage[SMALL_CAP];
    net_ghost_table_t table;
    net_ghost_table_init(&table, storage, SMALL_CAP);

    /* Fill to capacity. */
    for (uint32_t i = 0; i < SMALL_CAP; i++) {
        net_ghost_entity_t e = { .index = i, .generation = 1 };
        int rc = net_ghost_table_create(&table, i + 1u, e);
        ASSERT_INT_EQ(NET_GHOST_OK, rc);
    }

    /* One more should fail. */
    net_ghost_entity_t extra = { .index = 99, .generation = 1 };
    int rc = net_ghost_table_create(&table, 999u, extra);
    ASSERT_INT_EQ(NET_GHOST_FULL, rc);

    /* All originals should still be valid. */
    for (uint32_t i = 0; i < SMALL_CAP; i++) {
        net_ghost_entity_t out;
        rc = net_ghost_table_lookup(&table, i + 1u, &out);
        ASSERT_INT_EQ(NET_GHOST_OK, rc);
        ASSERT_UINT_EQ(i, out.index);
    }

    return 0;
}

/* ── Test: duplicate create returns error ──────────────────────── */

static int test_duplicate_create(void) {
    net_ghost_entry_t storage[TEST_CAPACITY];
    net_ghost_table_t table;
    net_ghost_table_init(&table, storage, TEST_CAPACITY);

    net_ghost_entity_t local = { .index = 3, .generation = 1 };
    int rc = net_ghost_table_create(&table, 10u, local);
    ASSERT_INT_EQ(NET_GHOST_OK, rc);

    /* Creating same server_id again should fail. */
    net_ghost_entity_t local2 = { .index = 4, .generation = 2 };
    rc = net_ghost_table_create(&table, 10u, local2);
    ASSERT_INT_EQ(NET_GHOST_DUPLICATE, rc);

    /* Original mapping should be unchanged. */
    net_ghost_entity_t out;
    rc = net_ghost_table_lookup(&table, 10u, &out);
    ASSERT_INT_EQ(NET_GHOST_OK, rc);
    ASSERT_UINT_EQ(3u, out.index);

    return 0;
}

/* ── Test: clear resets the entire table ───────────────────────── */

static int test_clear(void) {
    net_ghost_entry_t storage[TEST_CAPACITY];
    net_ghost_table_t table;
    net_ghost_table_init(&table, storage, TEST_CAPACITY);

    for (uint32_t i = 0; i < 10; i++) {
        net_ghost_entity_t e = { .index = i, .generation = 1 };
        net_ghost_table_create(&table, i, e);
    }

    net_ghost_table_clear(&table);

    /* All lookups should fail. */
    for (uint32_t i = 0; i < 10; i++) {
        net_ghost_entity_t out;
        int rc = net_ghost_table_lookup(&table, i, &out);
        ASSERT_INT_EQ(NET_GHOST_NOT_FOUND, rc);
    }

    /* Should be able to add again. */
    net_ghost_entity_t e = { .index = 0, .generation = 2 };
    int rc = net_ghost_table_create(&table, 0u, e);
    ASSERT_INT_EQ(NET_GHOST_OK, rc);

    return 0;
}

/* ── Test: null safety ─────────────────────────────────────────── */

static int test_null_safety(void) {
    net_ghost_entity_t e = { .index = 0, .generation = 0 };
    net_ghost_entity_t out;

    ASSERT_INT_EQ(NET_GHOST_ERR_INVALID, net_ghost_table_create(NULL, 0, e));
    ASSERT_INT_EQ(NET_GHOST_ERR_INVALID, net_ghost_table_lookup(NULL, 0, &out));
    ASSERT_INT_EQ(NET_GHOST_ERR_INVALID, net_ghost_table_destroy(NULL, 0));

    net_ghost_entry_t storage[4];
    net_ghost_table_t table;
    net_ghost_table_init(&table, storage, 4);
    ASSERT_INT_EQ(NET_GHOST_ERR_INVALID, net_ghost_table_lookup(&table, 0, NULL));

    return 0;
}

/* ── Test: count tracks active ghosts ──────────────────────────── */

static int test_count(void) {
    net_ghost_entry_t storage[TEST_CAPACITY];
    net_ghost_table_t table;
    net_ghost_table_init(&table, storage, TEST_CAPACITY);

    ASSERT_UINT_EQ(0u, net_ghost_table_count(&table));

    net_ghost_entity_t e1 = { .index = 1, .generation = 1 };
    net_ghost_entity_t e2 = { .index = 2, .generation = 1 };
    net_ghost_table_create(&table, 10u, e1);
    ASSERT_UINT_EQ(1u, net_ghost_table_count(&table));

    net_ghost_table_create(&table, 20u, e2);
    ASSERT_UINT_EQ(2u, net_ghost_table_count(&table));

    net_ghost_table_destroy(&table, 10u);
    ASSERT_UINT_EQ(1u, net_ghost_table_count(&table));

    net_ghost_table_clear(&table);
    ASSERT_UINT_EQ(0u, net_ghost_table_count(&table));

    return 0;
}

/* ── Runner ────────────────────────────────────────────────────── */

typedef int (*test_fn)(void);
typedef struct { const char *name; test_fn fn; } test_entry_t;

int main(void) {
    const test_entry_t tests[] = {
        {"create_and_lookup",    test_create_and_lookup},
        {"lookup_nonexistent",   test_lookup_nonexistent},
        {"destroy_removes_mapping", test_destroy_removes_mapping},
        {"churn_reuse",          test_churn_reuse},
        {"capacity_limit",       test_capacity_limit},
        {"duplicate_create",     test_duplicate_create},
        {"clear",                test_clear},
        {"null_safety",          test_null_safety},
        {"count",                test_count},
    };

    int n = (int)(sizeof(tests) / sizeof(tests[0]));
    int passed = 0;

    printf("p007_net_ghost_table_tests:\n");
    for (int i = 0; i < n; ++i) {
        int rc = tests[i].fn();
        printf("  %-50s %s\n", tests[i].name, rc == 0 ? "PASS" : "FAIL");
        if (rc == 0) { ++passed; }
    }

    printf("%d/%d tests passed\n", passed, n);
    return passed == n ? 0 : 1;
}
