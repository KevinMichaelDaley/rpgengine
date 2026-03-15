/**
 * @file pivot_edit_tests.c
 * @brief Tests for pivot edit mode in the scene editor.
 *
 * Covers toggle behavior, single-selection requirement, pivot reset,
 * and pivot world-position computation.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "ferrum/editor/scene/scene_ui.h"
#include "ferrum/editor/edit_selection.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/math/quat.h"

#define ASSERT_TRUE(cond)                                                      \
    do {                                                                        \
        if (!(cond)) {                                                          \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n",                \
                    __FILE__, __LINE__, #cond);                                \
            return 1;                                                          \
        }                                                                      \
    } while (0)

#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))

#define ASSERT_FLOAT_EQ(exp, act, eps)                                         \
    do {                                                                        \
        float _e = (exp), _a = (act);                                          \
        if (fabsf(_e - _a) > (eps)) {                                          \
            fprintf(stderr,                                                    \
                    "ASSERT_FLOAT_EQ failed: %s:%d: expected %.8f got %.8f\n", \
                    __FILE__, __LINE__, (double)_e, (double)_a);               \
            return 1;                                                          \
        }                                                                      \
    } while (0)

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

/* -------------------------------------------------------------------------- */
/* Test: pivot mode toggle                                                     */
/* -------------------------------------------------------------------------- */
static int test_pivot_mode_toggle(void) {
    scene_ui_state_t ui;
    memset(&ui, 0, sizeof(ui));

    /* Initially off. */
    ASSERT_FALSE(ui.pivot_edit_mode);

    /* Toggle on. */
    ui.pivot_edit_mode = true;
    ASSERT_TRUE(ui.pivot_edit_mode);

    /* Toggle off. */
    ui.pivot_edit_mode = false;
    ASSERT_FALSE(ui.pivot_edit_mode);

    /* Toggle back on, then off again. */
    ui.pivot_edit_mode = true;
    ASSERT_TRUE(ui.pivot_edit_mode);
    ui.pivot_edit_mode = false;
    ASSERT_FALSE(ui.pivot_edit_mode);

    return 0;
}

/* -------------------------------------------------------------------------- */
/* Test: pivot mode requires single selection                                  */
/* -------------------------------------------------------------------------- */
static int test_pivot_mode_requires_single_selection(void) {
    scene_ui_state_t ui;
    memset(&ui, 0, sizeof(ui));

    edit_selection_t sel;
    ASSERT_TRUE(edit_selection_init(&sel));

    /* No selection: pivot mode should NOT activate. */
    uint32_t sel_count = edit_selection_count(&sel);
    bool can_enter = (sel_count == 1);
    ASSERT_FALSE(can_enter);

    /* Two entities selected: pivot mode should NOT activate. */
    edit_selection_add(&sel, 0);
    edit_selection_add(&sel, 1);
    sel_count = edit_selection_count(&sel);
    can_enter = (sel_count == 1);
    ASSERT_FALSE(can_enter);

    /* Exactly one entity selected: pivot mode CAN activate. */
    edit_selection_remove(&sel, 1);
    sel_count = edit_selection_count(&sel);
    can_enter = (sel_count == 1);
    ASSERT_TRUE(can_enter);
    if (can_enter) {
        ui.pivot_edit_mode = true;
    }
    ASSERT_TRUE(ui.pivot_edit_mode);

    /* If we already in pivot mode and toggle off, it works regardless
     * of selection count. */
    ui.pivot_edit_mode = false;
    ASSERT_FALSE(ui.pivot_edit_mode);

    edit_selection_destroy(&sel);
    return 0;
}

/* -------------------------------------------------------------------------- */
/* Test: pivot reset sets offset to {0,0,0}                                    */
/* -------------------------------------------------------------------------- */
static int test_pivot_reset(void) {
    edit_entity_store_t store;
    ASSERT_TRUE(edit_entity_store_init(&store, 4));

    /* Create an entity with a non-zero pivot offset. */
    uint32_t id = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_BOX);
    ASSERT_TRUE(id != EDIT_ENTITY_INVALID_ID);
    edit_entity_t *ent = edit_entity_store_get_mut(&store, id);
    ASSERT_TRUE(ent != NULL);
    ent->pivot_offset[0] = 1.5f;
    ent->pivot_offset[1] = -2.0f;
    ent->pivot_offset[2] = 3.7f;

    /* Reset pivot offset. */
    ent->pivot_offset[0] = 0.0f;
    ent->pivot_offset[1] = 0.0f;
    ent->pivot_offset[2] = 0.0f;

    ASSERT_FLOAT_EQ(0.0f, ent->pivot_offset[0], 1e-6f);
    ASSERT_FLOAT_EQ(0.0f, ent->pivot_offset[1], 1e-6f);
    ASSERT_FLOAT_EQ(0.0f, ent->pivot_offset[2], 1e-6f);

    edit_entity_store_destroy(&store);
    return 0;
}

/* -------------------------------------------------------------------------- */
/* Test: pivot world position = pos + quat_rotate(orientation, pivot_offset)   */
/* -------------------------------------------------------------------------- */
static int test_pivot_world_position(void) {
    edit_entity_store_t store;
    ASSERT_TRUE(edit_entity_store_init(&store, 4));

    uint32_t id = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_BOX);
    ASSERT_TRUE(id != EDIT_ENTITY_INVALID_ID);
    edit_entity_t *ent = edit_entity_store_get_mut(&store, id);
    ASSERT_TRUE(ent != NULL);

    /* Entity at position (10, 20, 30). */
    ent->pos[0] = 10.0f;
    ent->pos[1] = 20.0f;
    ent->pos[2] = 30.0f;

    /* Identity orientation: pivot world pos = pos + pivot_offset. */
    ent->orientation = (quat_t){0.0f, 0.0f, 0.0f, 1.0f};
    ent->pivot_offset[0] = 1.0f;
    ent->pivot_offset[1] = 2.0f;
    ent->pivot_offset[2] = 3.0f;

    vec3_t plocal = {ent->pivot_offset[0], ent->pivot_offset[1],
                     ent->pivot_offset[2]};
    vec3_t pworld = quat_rotate_vec3(ent->orientation, plocal);
    float wx = ent->pos[0] + pworld.x;
    float wy = ent->pos[1] + pworld.y;
    float wz = ent->pos[2] + pworld.z;

    ASSERT_FLOAT_EQ(11.0f, wx, 1e-5f);
    ASSERT_FLOAT_EQ(22.0f, wy, 1e-5f);
    ASSERT_FLOAT_EQ(33.0f, wz, 1e-5f);

    /* 90-degree rotation around Y axis: local X(1,0,0) becomes Z(0,0,-1).
     * So pivot_offset (1, 2, 3) rotated by 90° around Y:
     * x -> z (becomes -1), y stays, z -> -x (becomes 3).
     * Actually: Ry(90°): (x,y,z) -> (z, y, -x).
     * So (1,2,3) -> (3, 2, -1). */
    ent->orientation = quat_from_axis_angle(
        (vec3_t){0.0f, 1.0f, 0.0f}, 3.14159265f / 2.0f, 1e-6f);
    pworld = quat_rotate_vec3(ent->orientation, plocal);
    wx = ent->pos[0] + pworld.x;
    wy = ent->pos[1] + pworld.y;
    wz = ent->pos[2] + pworld.z;

    ASSERT_FLOAT_EQ(10.0f + 3.0f, wx, 1e-4f);
    ASSERT_FLOAT_EQ(20.0f + 2.0f, wy, 1e-4f);
    ASSERT_FLOAT_EQ(30.0f - 1.0f, wz, 1e-4f);

    /* Zero pivot offset always gives entity position regardless of
     * orientation. */
    ent->pivot_offset[0] = 0.0f;
    ent->pivot_offset[1] = 0.0f;
    ent->pivot_offset[2] = 0.0f;
    plocal = (vec3_t){0.0f, 0.0f, 0.0f};
    pworld = quat_rotate_vec3(ent->orientation, plocal);
    wx = ent->pos[0] + pworld.x;
    wy = ent->pos[1] + pworld.y;
    wz = ent->pos[2] + pworld.z;

    ASSERT_FLOAT_EQ(10.0f, wx, 1e-6f);
    ASSERT_FLOAT_EQ(20.0f, wy, 1e-6f);
    ASSERT_FLOAT_EQ(30.0f, wz, 1e-6f);

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
    {"pivot_mode_toggle",                    test_pivot_mode_toggle},
    {"pivot_mode_requires_single_selection", test_pivot_mode_requires_single_selection},
    {"pivot_reset",                          test_pivot_reset},
    {"pivot_world_position",                 test_pivot_world_position},
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
