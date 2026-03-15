/**
 * @file edit_entity_version_tests.c
 * @brief Tests for the entity version tracking system.
 *
 * Covers init/destroy, stamp, tombstone ring buffer, needs_full_resync,
 * count_changed, get_changed_ids, out-of-range safety, and mixed operations.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/editor/edit_entity_version.h"

#define ASSERT_TRUE(cond)                                                                            \
    do {                                                                                             \
        if (!(cond)) {                                                                               \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n", __FILE__, __LINE__, #cond);           \
            return 1;                                                                                \
        }                                                                                            \
    } while (0)

#define ASSERT_UINT_EQ(exp, act)                                                                     \
    do {                                                                                             \
        if ((uint64_t)(exp) != (uint64_t)(act)) {                                                    \
            fprintf(stderr, "ASSERT_UINT_EQ failed: %s:%d: expected %llu got %llu\n",                \
                    __FILE__, __LINE__,                                                              \
                    (unsigned long long)(exp), (unsigned long long)(act));                            \
            return 1;                                                                                \
        }                                                                                            \
    } while (0)

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

/* -------------------------------------------------------------------------- */
/* Test: init sets all fields to zero/default, destroy cleans up              */
/* -------------------------------------------------------------------------- */
static int test_init_destroy(void) {
    edit_version_state_t state;
    ASSERT_TRUE(edit_version_init(&state, 64));

    ASSERT_UINT_EQ(0, state.version);
    ASSERT_UINT_EQ(64, state.entity_capacity);
    ASSERT_TRUE(state.entity_version != NULL);
    ASSERT_UINT_EQ(0, state.tombstone_head);
    ASSERT_UINT_EQ(0, state.tombstone_count);
    ASSERT_UINT_EQ(EDIT_VERSION_TOMBSTONE_CAP, state.tombstone_capacity);
    ASSERT_TRUE(state.tombstones != NULL);

    /* All entity versions should be zero after init. */
    for (uint32_t i = 0; i < 64; ++i) {
        ASSERT_UINT_EQ(0, state.entity_version[i]);
    }

    edit_version_destroy(&state);
    return 0;
}

/* -------------------------------------------------------------------------- */
/* Test: stamp single entity increments global version and sets entity ver    */
/* -------------------------------------------------------------------------- */
static int test_stamp_single(void) {
    edit_version_state_t state;
    ASSERT_TRUE(edit_version_init(&state, 16));

    edit_version_stamp(&state, 3);
    ASSERT_UINT_EQ(1, state.version);
    ASSERT_UINT_EQ(1, state.entity_version[3]);

    /* Other entities remain at 0. */
    ASSERT_UINT_EQ(0, state.entity_version[0]);
    ASSERT_UINT_EQ(0, state.entity_version[15]);

    edit_version_destroy(&state);
    return 0;
}

/* -------------------------------------------------------------------------- */
/* Test: stamp multiple entities, each gets a distinct version                */
/* -------------------------------------------------------------------------- */
static int test_stamp_multiple(void) {
    edit_version_state_t state;
    ASSERT_TRUE(edit_version_init(&state, 16));

    edit_version_stamp(&state, 0);
    edit_version_stamp(&state, 5);
    edit_version_stamp(&state, 0); /* stamp entity 0 again */

    ASSERT_UINT_EQ(3, state.version);
    /* Entity 0 was stamped twice; should have the latest version (3). */
    ASSERT_UINT_EQ(3, state.entity_version[0]);
    /* Entity 5 was stamped once at version 2. */
    ASSERT_UINT_EQ(2, state.entity_version[5]);
    /* Untouched entity. */
    ASSERT_UINT_EQ(0, state.entity_version[1]);

    edit_version_destroy(&state);
    return 0;
}

/* -------------------------------------------------------------------------- */
/* Test: tombstone single entity                                              */
/* -------------------------------------------------------------------------- */
static int test_tombstone_single(void) {
    edit_version_state_t state;
    ASSERT_TRUE(edit_version_init(&state, 16));

    edit_version_stamp(&state, 7); /* version -> 1 */
    edit_version_tombstone(&state, 7); /* version -> 2 */

    ASSERT_UINT_EQ(2, state.version);
    ASSERT_UINT_EQ(1, state.tombstone_count);
    ASSERT_UINT_EQ(1, state.tombstone_head); /* advanced past slot 0 */
    ASSERT_UINT_EQ(7, state.tombstones[0].entity_id);
    ASSERT_UINT_EQ(2, state.tombstones[0].version);

    edit_version_destroy(&state);
    return 0;
}

/* -------------------------------------------------------------------------- */
/* Test: tombstone ring wraparound (fill beyond capacity)                     */
/* -------------------------------------------------------------------------- */
static int test_tombstone_ring_wraparound(void) {
    edit_version_state_t state;
    ASSERT_TRUE(edit_version_init(&state, 8));

    /* Fill tombstone ring to capacity and then some. */
    uint32_t cap = state.tombstone_capacity;
    for (uint32_t i = 0; i < cap + 10; ++i) {
        edit_version_tombstone(&state, i % 8);
    }

    /* Count should be capped at capacity. */
    ASSERT_UINT_EQ(cap, state.tombstone_count);
    /* Head should have wrapped: (cap + 10) % cap == 10 */
    ASSERT_UINT_EQ(10, state.tombstone_head);
    /* Global version should reflect all operations. */
    ASSERT_UINT_EQ(cap + 10, state.version);

    edit_version_destroy(&state);
    return 0;
}

/* -------------------------------------------------------------------------- */
/* Test: needs_full_resync returns true when since_version is 0               */
/* -------------------------------------------------------------------------- */
static int test_needs_full_resync_since_zero(void) {
    edit_version_state_t state;
    ASSERT_TRUE(edit_version_init(&state, 8));

    /* Even with no changes, since_version=0 should trigger full resync. */
    ASSERT_TRUE(edit_version_needs_full_resync(&state, 0));

    edit_version_stamp(&state, 1);
    ASSERT_TRUE(edit_version_needs_full_resync(&state, 0));

    edit_version_destroy(&state);
    return 0;
}

/* -------------------------------------------------------------------------- */
/* Test: needs_full_resync returns true when tombstones wrapped past since    */
/* -------------------------------------------------------------------------- */
static int test_needs_full_resync_tombstone_wrap(void) {
    edit_version_state_t state;
    ASSERT_TRUE(edit_version_init(&state, 8));

    /* Record the version before filling tombstones. */
    edit_version_stamp(&state, 0); /* version = 1 */
    uint64_t old_version = state.version;

    /* Fill the entire tombstone ring so that it wraps past old_version. */
    uint32_t cap = state.tombstone_capacity;
    for (uint32_t i = 0; i < cap; ++i) {
        edit_version_tombstone(&state, i % 8);
    }

    /* The oldest tombstone now has version 2, which is > old_version(1).
     * Since count == capacity, the ring has wrapped past old_version. */
    ASSERT_TRUE(edit_version_needs_full_resync(&state, old_version));

    edit_version_destroy(&state);
    return 0;
}

/* -------------------------------------------------------------------------- */
/* Test: needs_full_resync returns false for recent since_version             */
/* -------------------------------------------------------------------------- */
static int test_needs_full_resync_false_recent(void) {
    edit_version_state_t state;
    ASSERT_TRUE(edit_version_init(&state, 8));

    /* Do a few stamps and tombstones but don't fill the ring. */
    edit_version_stamp(&state, 0);
    edit_version_stamp(&state, 1);
    edit_version_tombstone(&state, 2);

    uint64_t recent = state.version; /* version = 3 */

    /* One more stamp after. */
    edit_version_stamp(&state, 3);

    /* Since we haven't filled the ring, and since_version is recent. */
    ASSERT_TRUE(!edit_version_needs_full_resync(&state, recent));

    edit_version_destroy(&state);
    return 0;
}

/* -------------------------------------------------------------------------- */
/* Test: count_changed with no changes returns 0                              */
/* -------------------------------------------------------------------------- */
static int test_count_changed_no_changes(void) {
    edit_version_state_t state;
    ASSERT_TRUE(edit_version_init(&state, 16));

    ASSERT_UINT_EQ(0, edit_version_count_changed(&state, 0));
    /* Even with a non-zero since_version, nothing has changed. */
    ASSERT_UINT_EQ(0, edit_version_count_changed(&state, 5));

    edit_version_destroy(&state);
    return 0;
}

/* -------------------------------------------------------------------------- */
/* Test: count_changed after stamps returns correct count                     */
/* -------------------------------------------------------------------------- */
static int test_count_changed_after_stamps(void) {
    edit_version_state_t state;
    ASSERT_TRUE(edit_version_init(&state, 16));

    edit_version_stamp(&state, 0); /* ver 1 */
    edit_version_stamp(&state, 3); /* ver 2 */
    edit_version_stamp(&state, 7); /* ver 3 */

    /* All 3 entities changed since version 0. */
    ASSERT_UINT_EQ(3, edit_version_count_changed(&state, 0));

    /* Only entities stamped after version 1. */
    ASSERT_UINT_EQ(2, edit_version_count_changed(&state, 1));

    /* Only entity 7 stamped after version 2. */
    ASSERT_UINT_EQ(1, edit_version_count_changed(&state, 2));

    /* Nothing changed after version 3. */
    ASSERT_UINT_EQ(0, edit_version_count_changed(&state, 3));

    edit_version_destroy(&state);
    return 0;
}

/* -------------------------------------------------------------------------- */
/* Test: get_changed_ids returns correct IDs                                  */
/* -------------------------------------------------------------------------- */
static int test_get_changed_ids_correct(void) {
    edit_version_state_t state;
    ASSERT_TRUE(edit_version_init(&state, 16));

    edit_version_stamp(&state, 2); /* ver 1 */
    edit_version_stamp(&state, 5); /* ver 2 */
    edit_version_stamp(&state, 9); /* ver 3 */

    uint32_t ids[16];
    uint32_t count = edit_version_get_changed_ids(&state, 0, ids, 16);
    ASSERT_UINT_EQ(3, count);

    /* IDs should be in scan order (ascending entity index). */
    ASSERT_UINT_EQ(2, ids[0]);
    ASSERT_UINT_EQ(5, ids[1]);
    ASSERT_UINT_EQ(9, ids[2]);

    /* Query with since_version = 1: only entities 5 and 9. */
    count = edit_version_get_changed_ids(&state, 1, ids, 16);
    ASSERT_UINT_EQ(2, count);
    ASSERT_UINT_EQ(5, ids[0]);
    ASSERT_UINT_EQ(9, ids[1]);

    edit_version_destroy(&state);
    return 0;
}

/* -------------------------------------------------------------------------- */
/* Test: get_changed_ids respects max_ids limit                               */
/* -------------------------------------------------------------------------- */
static int test_get_changed_ids_max_limit(void) {
    edit_version_state_t state;
    ASSERT_TRUE(edit_version_init(&state, 16));

    edit_version_stamp(&state, 0);
    edit_version_stamp(&state, 1);
    edit_version_stamp(&state, 2);
    edit_version_stamp(&state, 3);

    uint32_t ids[2];
    uint32_t count = edit_version_get_changed_ids(&state, 0, ids, 2);
    ASSERT_UINT_EQ(2, count);
    /* Should get the first 2 in scan order. */
    ASSERT_UINT_EQ(0, ids[0]);
    ASSERT_UINT_EQ(1, ids[1]);

    edit_version_destroy(&state);
    return 0;
}

/* -------------------------------------------------------------------------- */
/* Test: stamp with out-of-range entity ID is a no-op                         */
/* -------------------------------------------------------------------------- */
static int test_stamp_out_of_range(void) {
    edit_version_state_t state;
    ASSERT_TRUE(edit_version_init(&state, 8));

    uint64_t ver_before = state.version;
    edit_version_stamp(&state, 8);  /* exactly at capacity -- out of range */
    edit_version_stamp(&state, 99); /* way out of range */

    /* Version should not have changed. */
    ASSERT_UINT_EQ(ver_before, state.version);

    /* No entity should be marked changed. */
    ASSERT_UINT_EQ(0, edit_version_count_changed(&state, 0));

    edit_version_destroy(&state);
    return 0;
}

/* -------------------------------------------------------------------------- */
/* Test: mixed stamps and tombstones                                          */
/* -------------------------------------------------------------------------- */
static int test_mixed_stamps_and_tombstones(void) {
    edit_version_state_t state;
    ASSERT_TRUE(edit_version_init(&state, 16));

    edit_version_stamp(&state, 0);     /* ver 1 */
    edit_version_stamp(&state, 1);     /* ver 2 */
    uint64_t checkpoint = state.version; /* 2 */

    edit_version_tombstone(&state, 0); /* ver 3 -- entity 0 deleted */
    edit_version_stamp(&state, 2);     /* ver 4 */

    /* Entities changed since checkpoint: entity 2 (stamped at ver 4).
     * Entity 1 was stamped at ver 2, which is NOT > checkpoint(2).
     * Entity 0 was stamped at ver 1, also not > checkpoint. */
    ASSERT_UINT_EQ(1, edit_version_count_changed(&state, checkpoint));

    uint32_t ids[16];
    uint32_t count = edit_version_get_changed_ids(&state, checkpoint, ids, 16);
    ASSERT_UINT_EQ(1, count);
    ASSERT_UINT_EQ(2, ids[0]);

    /* Tombstone count should be 1. */
    ASSERT_UINT_EQ(1, state.tombstone_count);
    ASSERT_UINT_EQ(0, state.tombstones[0].entity_id);
    ASSERT_UINT_EQ(3, state.tombstones[0].version);

    /* No full resync needed -- tombstones haven't wrapped. */
    ASSERT_TRUE(!edit_version_needs_full_resync(&state, checkpoint));

    edit_version_destroy(&state);
    return 0;
}

/* -------------------------------------------------------------------------- */
/* Test table and main                                                        */
/* -------------------------------------------------------------------------- */

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    {"init_destroy",                       test_init_destroy},
    {"stamp_single",                       test_stamp_single},
    {"stamp_multiple",                     test_stamp_multiple},
    {"tombstone_single",                   test_tombstone_single},
    {"tombstone_ring_wraparound",          test_tombstone_ring_wraparound},
    {"needs_full_resync_since_zero",       test_needs_full_resync_since_zero},
    {"needs_full_resync_tombstone_wrap",   test_needs_full_resync_tombstone_wrap},
    {"needs_full_resync_false_recent",     test_needs_full_resync_false_recent},
    {"count_changed_no_changes",           test_count_changed_no_changes},
    {"count_changed_after_stamps",         test_count_changed_after_stamps},
    {"get_changed_ids_correct",            test_get_changed_ids_correct},
    {"get_changed_ids_max_limit",          test_get_changed_ids_max_limit},
    {"stamp_out_of_range",                 test_stamp_out_of_range},
    {"mixed_stamps_and_tombstones",        test_mixed_stamps_and_tombstones},
};

int main(void) {
    size_t total = ARRAY_SIZE(TESTS);
    size_t passed = 0;
    for (size_t i = 0; i < total; ++i) {
        struct test_case *tc = &TESTS[i];
        printf("RUN %s\n", tc->name);
        int rc = tc->fn();
        if (rc == 0) {
            printf("OK %s\n", tc->name);
            passed++;
        } else {
            fprintf(stderr, "FAIL %s (rc=%d)\n", tc->name, rc);
            break;
        }
    }
    if (passed == total) {
        printf("All %zu tests passed\n", passed);
        return 0;
    }
    return 1;
}
