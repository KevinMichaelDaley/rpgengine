/**
 * @file entity_hide_tests.c
 * @brief Tests for entity hide/show feature.
 *
 * Covers: default hidden state, hiding selected entities, showing all
 * hidden entities, and hidden entities being excluded from picking.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_selection.h"

#define ASSERT_TRUE(cond)                                                    \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n",              \
                    __FILE__, __LINE__, #cond);                              \
            return 1;                                                        \
        }                                                                    \
    } while (0)

#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))

#define ASSERT_UINT_EQ(exp, act)                                             \
    do {                                                                     \
        if ((uint64_t)(exp) != (uint64_t)(act)) {                            \
            fprintf(stderr, "ASSERT_UINT_EQ failed: %s:%d: expected %llu "   \
                    "got %llu\n", __FILE__, __LINE__,                        \
                    (unsigned long long)(exp), (unsigned long long)(act));    \
            return 1;                                                        \
        }                                                                    \
    } while (0)

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

/* -------------------------------------------------------------------------- */
/* Test: newly created entity has hidden=false                                */
/* -------------------------------------------------------------------------- */
static int test_hidden_default_false(void) {
    edit_entity_store_t store;
    ASSERT_TRUE(edit_entity_store_init(&store, 16));

    uint32_t id = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_BOX);
    ASSERT_TRUE(id != EDIT_ENTITY_INVALID_ID);

    const edit_entity_t *ent = edit_entity_store_get(&store, id);
    ASSERT_TRUE(ent != NULL);
    ASSERT_FALSE(ent->hidden);

    edit_entity_store_destroy(&store);
    return 0;
}

/* -------------------------------------------------------------------------- */
/* Test: hiding selected entities sets hidden=true and deselects them         */
/* -------------------------------------------------------------------------- */
static int test_hide_selected(void) {
    edit_entity_store_t store;
    ASSERT_TRUE(edit_entity_store_init(&store, 16));

    edit_selection_t sel;
    ASSERT_TRUE(edit_selection_init(&sel));

    /* Create three entities. */
    uint32_t id0 = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_BOX);
    uint32_t id1 = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_SPHERE);
    uint32_t id2 = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_CAPSULE);

    /* Select first two. */
    edit_selection_add(&sel, id0);
    edit_selection_add(&sel, id1);
    ASSERT_UINT_EQ(2, edit_selection_count(&sel));

    /* Hide selected entities (simulating what the H key handler does). */
    uint32_t sel_count = edit_selection_count(&sel);
    const uint32_t *sel_ids = edit_selection_ids(&sel);
    for (uint32_t si = 0; si < sel_count; ++si) {
        edit_entity_t *ent = edit_entity_store_get_mut(&store, sel_ids[si]);
        if (ent) ent->hidden = true;
    }
    edit_selection_clear(&sel);

    /* Verify: id0 and id1 are hidden, id2 is not. */
    ASSERT_TRUE(edit_entity_store_get(&store, id0)->hidden);
    ASSERT_TRUE(edit_entity_store_get(&store, id1)->hidden);
    ASSERT_FALSE(edit_entity_store_get(&store, id2)->hidden);

    /* Verify: selection is empty. */
    ASSERT_UINT_EQ(0, edit_selection_count(&sel));

    edit_selection_destroy(&sel);
    edit_entity_store_destroy(&store);
    return 0;
}

/* -------------------------------------------------------------------------- */
/* Test: show_all sets hidden=false on all entities                           */
/* -------------------------------------------------------------------------- */
static int test_show_all(void) {
    edit_entity_store_t store;
    ASSERT_TRUE(edit_entity_store_init(&store, 16));

    /* Create entities and hide some. */
    uint32_t id0 = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_BOX);
    uint32_t id1 = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_SPHERE);
    uint32_t id2 = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_CAPSULE);

    edit_entity_store_get_mut(&store, id0)->hidden = true;
    edit_entity_store_get_mut(&store, id1)->hidden = true;

    /* Simulate Shift+H: show all. */
    for (uint32_t i = 0; i < store.capacity; ++i) {
        edit_entity_t *ent = edit_entity_store_get_mut(&store, i);
        if (ent) ent->hidden = false;
    }

    /* Verify all are visible. */
    ASSERT_FALSE(edit_entity_store_get(&store, id0)->hidden);
    ASSERT_FALSE(edit_entity_store_get(&store, id1)->hidden);
    ASSERT_FALSE(edit_entity_store_get(&store, id2)->hidden);

    edit_entity_store_destroy(&store);
    return 0;
}

/* -------------------------------------------------------------------------- */
/* Test: hidden entities are skipped in the picking candidate loop            */
/* -------------------------------------------------------------------------- */
static int test_hidden_entities_not_pickable(void) {
    edit_entity_store_t store;
    ASSERT_TRUE(edit_entity_store_init(&store, 16));

    uint32_t id0 = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_BOX);
    uint32_t id1 = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_SPHERE);
    uint32_t id2 = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_CAPSULE);

    /* Hide entity 1. */
    edit_entity_store_get_mut(&store, id1)->hidden = true;

    /* Simulate the pick loop: collect non-hidden, non-deleted entities. */
    uint32_t pick_count = 0;
    uint32_t picked_ids[16];
    for (uint32_t i = 0; i < store.capacity; ++i) {
        const edit_entity_t *ent = edit_entity_store_get(&store, i);
        if (!ent || ent->pending_delete || ent->hidden) continue;
        picked_ids[pick_count++] = i;
    }

    /* Only id0 and id2 should be pickable. */
    ASSERT_UINT_EQ(2, pick_count);
    ASSERT_UINT_EQ(id0, picked_ids[0]);
    ASSERT_UINT_EQ(id2, picked_ids[1]);

    edit_entity_store_destroy(&store);
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
    {"hidden_default_false",          test_hidden_default_false},
    {"hide_selected",                 test_hide_selected},
    {"show_all",                      test_show_all},
    {"hidden_entities_not_pickable",  test_hidden_entities_not_pickable},
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
