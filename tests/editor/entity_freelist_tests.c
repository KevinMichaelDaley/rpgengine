/**
 * @file entity_freelist_tests.c
 * @brief Tests for the entity store freelist: O(1) create/remove/count.
 */

#include <stdio.h>
#include <string.h>
#include "ferrum/editor/edit_entity.h"

/* ----------------------------------------------------------------------- */
/* Test macros                                                               */
/* ----------------------------------------------------------------------- */

static int g_pass, g_fail;

#define RUN(fn) do { \
    printf("RUN  %s\n", #fn); \
    if (fn()) { printf("OK   %s\n", #fn); g_pass++; } \
    else       { printf("FAIL %s\n", #fn); g_fail++; } \
} while (0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("  ASSERT FAILED: %s (line %d)\n", #cond, __LINE__); \
        return false; \
    } \
} while (0)

/* ----------------------------------------------------------------------- */
/* Tests                                                                     */
/* ----------------------------------------------------------------------- */

/** Fresh store has count=0 and all capacity available. */
static bool test_init_empty(void) {
    edit_entity_store_t store;
    ASSERT(edit_entity_store_init(&store, 16));
    ASSERT(edit_entity_store_count(&store) == 0);
    edit_entity_store_destroy(&store);
    return true;
}

/** Create fills slots sequentially and count tracks. */
static bool test_create_sequential(void) {
    edit_entity_store_t store;
    ASSERT(edit_entity_store_init(&store, 8));

    uint32_t id0 = edit_entity_store_create(&store, 0);
    uint32_t id1 = edit_entity_store_create(&store, 0);
    uint32_t id2 = edit_entity_store_create(&store, 0);
    ASSERT(id0 != EDIT_ENTITY_INVALID_ID);
    ASSERT(id1 != EDIT_ENTITY_INVALID_ID);
    ASSERT(id2 != EDIT_ENTITY_INVALID_ID);
    /* All IDs must be distinct. */
    ASSERT(id0 != id1 && id1 != id2 && id0 != id2);
    ASSERT(edit_entity_store_count(&store) == 3);

    edit_entity_store_destroy(&store);
    return true;
}

/** Fill to capacity, then create returns INVALID_ID. */
static bool test_create_full(void) {
    edit_entity_store_t store;
    ASSERT(edit_entity_store_init(&store, 4));

    for (uint32_t i = 0; i < 4; i++) {
        ASSERT(edit_entity_store_create(&store, 0) != EDIT_ENTITY_INVALID_ID);
    }
    ASSERT(edit_entity_store_count(&store) == 4);
    ASSERT(edit_entity_store_create(&store, 0) == EDIT_ENTITY_INVALID_ID);

    edit_entity_store_destroy(&store);
    return true;
}

/** Remove returns slot to the freelist; count decrements. */
static bool test_remove_reclaims(void) {
    edit_entity_store_t store;
    ASSERT(edit_entity_store_init(&store, 4));

    uint32_t id0 = edit_entity_store_create(&store, 0);
    uint32_t id1 = edit_entity_store_create(&store, 0);
    ASSERT(edit_entity_store_count(&store) == 2);

    ASSERT(edit_entity_store_remove(&store, id0));
    ASSERT(edit_entity_store_count(&store) == 1);

    /* Slot should be reclaimable. */
    uint32_t id2 = edit_entity_store_create(&store, 0);
    ASSERT(id2 == id0); /* Freelist is LIFO — should reuse most-recently freed. */
    ASSERT(edit_entity_store_count(&store) == 2);

    (void)id1;
    edit_entity_store_destroy(&store);
    return true;
}

/** Remove same slot twice returns false the second time. */
static bool test_double_remove(void) {
    edit_entity_store_t store;
    ASSERT(edit_entity_store_init(&store, 4));

    uint32_t id = edit_entity_store_create(&store, 0);
    ASSERT(edit_entity_store_remove(&store, id));
    ASSERT(!edit_entity_store_remove(&store, id));
    ASSERT(edit_entity_store_count(&store) == 0);

    edit_entity_store_destroy(&store);
    return true;
}

/** Fill, remove all, refill — all slots reusable. */
static bool test_fill_remove_refill(void) {
    edit_entity_store_t store;
    ASSERT(edit_entity_store_init(&store, 8));

    uint32_t ids[8];
    for (uint32_t i = 0; i < 8; i++) {
        ids[i] = edit_entity_store_create(&store, 0);
        ASSERT(ids[i] != EDIT_ENTITY_INVALID_ID);
    }
    ASSERT(edit_entity_store_count(&store) == 8);

    for (uint32_t i = 0; i < 8; i++) {
        ASSERT(edit_entity_store_remove(&store, ids[i]));
    }
    ASSERT(edit_entity_store_count(&store) == 0);

    /* Refill — all 8 slots available again. */
    for (uint32_t i = 0; i < 8; i++) {
        ASSERT(edit_entity_store_create(&store, 0) != EDIT_ENTITY_INVALID_ID);
    }
    ASSERT(edit_entity_store_count(&store) == 8);

    edit_entity_store_destroy(&store);
    return true;
}

/** Restore (undo delete) reclaims a specific slot from the freelist. */
static bool test_restore_reclaims_slot(void) {
    edit_entity_store_t store;
    ASSERT(edit_entity_store_init(&store, 8));

    uint32_t id0 = edit_entity_store_create(&store, 0);
    uint32_t id1 = edit_entity_store_create(&store, 0);

    /* Snapshot id0 before removing. */
    edit_entity_t snapshot;
    memcpy(&snapshot, edit_entity_store_get(&store, id0), sizeof(snapshot));

    ASSERT(edit_entity_store_remove(&store, id0));
    ASSERT(edit_entity_store_count(&store) == 1);

    /* Restore id0 from snapshot. */
    ASSERT(edit_entity_store_restore(&store, id0, &snapshot));
    ASSERT(edit_entity_store_count(&store) == 2);

    /* id0 should be active again. */
    ASSERT(edit_entity_store_get(&store, id0) != NULL);
    ASSERT(edit_entity_store_get(&store, id0)->active);

    /* Creating another should NOT return id0 (it's restored/active). */
    uint32_t id2 = edit_entity_store_create(&store, 0);
    ASSERT(id2 != id0 && id2 != id1);
    ASSERT(edit_entity_store_count(&store) == 3);

    edit_entity_store_destroy(&store);
    return true;
}

/** Restore into an already-active slot returns false. */
static bool test_restore_active_fails(void) {
    edit_entity_store_t store;
    ASSERT(edit_entity_store_init(&store, 4));

    uint32_t id = edit_entity_store_create(&store, 0);
    edit_entity_t snapshot;
    memcpy(&snapshot, edit_entity_store_get(&store, id), sizeof(snapshot));

    /* Restore into active slot should fail. */
    ASSERT(!edit_entity_store_restore(&store, id, &snapshot));
    ASSERT(edit_entity_store_count(&store) == 1);

    edit_entity_store_destroy(&store);
    return true;
}

/** Large capacity init succeeds and count is O(1). */
static bool test_large_capacity(void) {
    edit_entity_store_t store;
    ASSERT(edit_entity_store_init(&store, 1000000));
    ASSERT(edit_entity_store_count(&store) == 0);

    /* Create a few entities — should be instant, not scanning 1M slots. */
    uint32_t id0 = edit_entity_store_create(&store, 0);
    uint32_t id1 = edit_entity_store_create(&store, 0);
    ASSERT(id0 != EDIT_ENTITY_INVALID_ID);
    ASSERT(id1 != EDIT_ENTITY_INVALID_ID);
    ASSERT(edit_entity_store_count(&store) == 2);

    edit_entity_store_destroy(&store);
    return true;
}

/* ----------------------------------------------------------------------- */
/* Main                                                                      */
/* ----------------------------------------------------------------------- */

int main(void) {
    RUN(test_init_empty);
    RUN(test_create_sequential);
    RUN(test_create_full);
    RUN(test_remove_reclaims);
    RUN(test_double_remove);
    RUN(test_fill_remove_refill);
    RUN(test_restore_reclaims_slot);
    RUN(test_restore_active_fails);
    RUN(test_large_capacity);

    printf("\n%d / %d tests passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}
