/**
 * @file p206b_pivot_tests.c
 * @brief Tests for pivot offset operations — snap to grid, reset,
 *        move to cursor, interaction with transforms.
 */

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_selection.h"
#include "ferrum/editor/scene/snap_state.h"

/* ---- Test harness ---- */

#define ASSERT_TRUE(cond)                                                      \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n", __FILE__,      \
                    __LINE__, #cond);                                          \
            return 1;                                                          \
        }                                                                      \
    } while (0)

#define ASSERT_FLOAT_NEAR(exp, act, tol)                                       \
    do {                                                                        \
        float _d = (float)(exp) - (float)(act);                                \
        if (_d < 0) _d = -_d;                                                 \
        if (_d > (tol)) {                                                     \
            fprintf(stderr, "ASSERT_FLOAT_NEAR failed: %s:%d: "              \
                    "expected %.6f got %.6f\n",                               \
                    __FILE__, __LINE__, (double)(exp), (double)(act));         \
            return 1;                                                          \
        }                                                                      \
    } while (0)

/* ---- Tests ---- */

static int test_pivot_default_zero(void) {
    edit_entity_store_t store;
    ASSERT_TRUE(edit_entity_store_init(&store, 16));
    uint32_t id = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_BOX);
    const edit_entity_t *e = edit_entity_store_get(&store, id);

    ASSERT_FLOAT_NEAR(0.0f, e->pivot_offset[0], 0.0001f);
    ASSERT_FLOAT_NEAR(0.0f, e->pivot_offset[1], 0.0001f);
    ASSERT_FLOAT_NEAR(0.0f, e->pivot_offset[2], 0.0001f);

    edit_entity_store_destroy(&store);
    return 0;
}

static int test_pivot_set_and_read(void) {
    edit_entity_store_t store;
    ASSERT_TRUE(edit_entity_store_init(&store, 16));
    uint32_t id = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_BOX);
    edit_entity_t *e = edit_entity_store_get_mut(&store, id);
    e->pivot_offset[0] = 1.5f;
    e->pivot_offset[1] = -2.0f;
    e->pivot_offset[2] = 0.25f;

    const edit_entity_t *ro = edit_entity_store_get(&store, id);
    ASSERT_FLOAT_NEAR(1.5f, ro->pivot_offset[0], 0.0001f);
    ASSERT_FLOAT_NEAR(-2.0f, ro->pivot_offset[1], 0.0001f);
    ASSERT_FLOAT_NEAR(0.25f, ro->pivot_offset[2], 0.0001f);

    edit_entity_store_destroy(&store);
    return 0;
}

static int test_pivot_reset(void) {
    edit_entity_store_t store;
    ASSERT_TRUE(edit_entity_store_init(&store, 16));
    uint32_t id = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_BOX);
    edit_entity_t *e = edit_entity_store_get_mut(&store, id);
    e->pivot_offset[0] = 5.0f;
    e->pivot_offset[1] = 3.0f;
    e->pivot_offset[2] = -1.0f;

    /* Reset pivot to origin. */
    e->pivot_offset[0] = 0.0f;
    e->pivot_offset[1] = 0.0f;
    e->pivot_offset[2] = 0.0f;

    ASSERT_FLOAT_NEAR(0.0f, e->pivot_offset[0], 0.0001f);
    ASSERT_FLOAT_NEAR(0.0f, e->pivot_offset[1], 0.0001f);
    ASSERT_FLOAT_NEAR(0.0f, e->pivot_offset[2], 0.0001f);

    edit_entity_store_destroy(&store);
    return 0;
}

static int test_pivot_snap_to_grid(void) {
    snap_state_t snap;
    snap_state_init(&snap);
    snap.enabled[SNAP_POSITION] = true;
    snap.grid_size[SNAP_POSITION] = 0.5f;

    /* Snap pivot offset values to grid. */
    float val = 1.3f;
    float snapped = snap_state_quantize(&snap, SNAP_POSITION, val, 0);
    ASSERT_FLOAT_NEAR(1.5f, snapped, 0.01f);

    /* Negative value. */
    float neg = -0.7f;
    float neg_snapped = snap_state_quantize(&snap, SNAP_POSITION, neg, 0);
    ASSERT_FLOAT_NEAR(-0.5f, neg_snapped, 0.01f);

    return 0;
}

static int test_pivot_move_to_cursor(void) {
    /* Simulate moving pivot to a 3D cursor position. */
    edit_entity_store_t store;
    ASSERT_TRUE(edit_entity_store_init(&store, 16));
    uint32_t id = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_BOX);
    edit_entity_t *e = edit_entity_store_get_mut(&store, id);
    e->pos[0] = 5.0f;
    e->pos[1] = 3.0f;
    e->pos[2] = -2.0f;

    /* 3D cursor at world (7, 3, 0). Pivot offset = cursor - entity pos. */
    float cursor[3] = {7.0f, 3.0f, 0.0f};
    e->pivot_offset[0] = cursor[0] - e->pos[0];
    e->pivot_offset[1] = cursor[1] - e->pos[1];
    e->pivot_offset[2] = cursor[2] - e->pos[2];

    ASSERT_FLOAT_NEAR(2.0f, e->pivot_offset[0], 0.001f);
    ASSERT_FLOAT_NEAR(0.0f, e->pivot_offset[1], 0.001f);
    ASSERT_FLOAT_NEAR(2.0f, e->pivot_offset[2], 0.001f);

    edit_entity_store_destroy(&store);
    return 0;
}

static int test_pivot_preserved_on_duplicate(void) {
    edit_entity_store_t store;
    ASSERT_TRUE(edit_entity_store_init(&store, 16));
    uint32_t id = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_SPHERE);
    edit_entity_t *e = edit_entity_store_get_mut(&store, id);
    e->pivot_offset[0] = 1.0f;
    e->pivot_offset[1] = 2.0f;
    e->pivot_offset[2] = 3.0f;

    /* Manual duplication. */
    uint32_t dup_id = edit_entity_store_create(&store, e->type);
    edit_entity_t *dup = edit_entity_store_get_mut(&store, dup_id);
    memcpy(dup->pivot_offset, e->pivot_offset, sizeof(dup->pivot_offset));

    ASSERT_FLOAT_NEAR(1.0f, dup->pivot_offset[0], 0.001f);
    ASSERT_FLOAT_NEAR(2.0f, dup->pivot_offset[1], 0.001f);
    ASSERT_FLOAT_NEAR(3.0f, dup->pivot_offset[2], 0.001f);

    edit_entity_store_destroy(&store);
    return 0;
}

/* ---- Test runner ---- */

struct test_case {
    const char *name;
    int (*fn)(void);
};

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static struct test_case TESTS[] = {
    {"pivot_default_zero",          test_pivot_default_zero},
    {"pivot_set_and_read",          test_pivot_set_and_read},
    {"pivot_reset",                 test_pivot_reset},
    {"pivot_snap_to_grid",          test_pivot_snap_to_grid},
    {"pivot_move_to_cursor",        test_pivot_move_to_cursor},
    {"pivot_preserved_on_duplicate", test_pivot_preserved_on_duplicate},
};

int main(void) {
    size_t total = ARRAY_SIZE(TESTS);
    size_t passed = 0;

    for (size_t i = 0; i < total; ++i) {
        struct test_case *tc = &TESTS[i];
        printf("RUN  %s\n", tc->name);
        int rc = tc->fn();
        if (rc == 0) {
            printf("  OK %s\n", tc->name);
            passed++;
        } else {
            fprintf(stderr, "FAIL %s\n", tc->name);
            break;
        }
    }

    printf("\n%zu / %zu tests passed\n", passed, total);
    return (passed == total) ? 0 : 1;
}
