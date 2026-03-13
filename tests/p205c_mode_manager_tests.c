/**
 * @file p205c_mode_manager_tests.c
 * @brief Tests for editor mode manager — vtable dispatch, mode switching,
 *        object mode transform operations.
 */

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/editor/mode/mode_manager.h"
#include "ferrum/editor/mode/mode_object.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_selection.h"

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

/* ---- Mode manager tests ---- */

static int test_mode_manager_init(void) {
    mode_manager_t mgr;
    mode_manager_init(&mgr);

    ASSERT_TRUE(mgr.active_mode == EDITOR_MODE_OBJECT);
    ASSERT_TRUE(mgr.mode_count > 0);

    mode_manager_destroy(&mgr);
    return 0;
}

static int test_mode_manager_get_active_name(void) {
    mode_manager_t mgr;
    mode_manager_init(&mgr);

    const char *name = mode_manager_active_name(&mgr);
    ASSERT_TRUE(name != NULL);
    ASSERT_TRUE(strcmp(name, "object") == 0);

    mode_manager_destroy(&mgr);
    return 0;
}

static int test_mode_manager_switch(void) {
    mode_manager_t mgr;
    mode_manager_init(&mgr);

    /* Switch to same mode — no-op, should not crash. */
    mode_manager_switch(&mgr, EDITOR_MODE_OBJECT);
    ASSERT_TRUE(mgr.active_mode == EDITOR_MODE_OBJECT);

    mode_manager_destroy(&mgr);
    return 0;
}

/* ---- Object mode tests ---- */

static int test_object_mode_translate(void) {
    edit_entity_store_t store;
    ASSERT_TRUE(edit_entity_store_init(&store, 16));
    uint32_t id = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_BOX);

    edit_selection_t sel;
    ASSERT_TRUE(edit_selection_init(&sel));
    edit_selection_add(&sel, id);

    /* Translate selected entities by (3, 0, -2). */
    float delta[3] = {3.0f, 0.0f, -2.0f};
    object_mode_translate(&store, &sel, delta);

    const edit_entity_t *e = edit_entity_store_get(&store, id);
    ASSERT_FLOAT_NEAR(3.0f, e->pos[0], 0.001f);
    ASSERT_FLOAT_NEAR(0.0f, e->pos[1], 0.001f);
    ASSERT_FLOAT_NEAR(-2.0f, e->pos[2], 0.001f);

    edit_selection_destroy(&sel);
    edit_entity_store_destroy(&store);
    return 0;
}

static int test_object_mode_rotate(void) {
    edit_entity_store_t store;
    ASSERT_TRUE(edit_entity_store_init(&store, 16));
    uint32_t id = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_BOX);

    edit_selection_t sel;
    ASSERT_TRUE(edit_selection_init(&sel));
    edit_selection_add(&sel, id);

    /* Rotate by (0, 45, 0) degrees. */
    float delta[3] = {0.0f, 45.0f, 0.0f};
    object_mode_rotate(&store, &sel, delta);

    const edit_entity_t *e = edit_entity_store_get(&store, id);
    ASSERT_FLOAT_NEAR(45.0f, e->rot[1], 0.001f);

    edit_selection_destroy(&sel);
    edit_entity_store_destroy(&store);
    return 0;
}

static int test_object_mode_scale(void) {
    edit_entity_store_t store;
    ASSERT_TRUE(edit_entity_store_init(&store, 16));
    uint32_t id = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_BOX);

    edit_selection_t sel;
    ASSERT_TRUE(edit_selection_init(&sel));
    edit_selection_add(&sel, id);

    /* Scale by (2, 2, 2). Entity starts at (1, 1, 1). */
    float delta[3] = {2.0f, 2.0f, 2.0f};
    object_mode_scale(&store, &sel, delta);

    const edit_entity_t *e = edit_entity_store_get(&store, id);
    ASSERT_FLOAT_NEAR(2.0f, e->scale[0], 0.001f);
    ASSERT_FLOAT_NEAR(2.0f, e->scale[1], 0.001f);
    ASSERT_FLOAT_NEAR(2.0f, e->scale[2], 0.001f);

    edit_selection_destroy(&sel);
    edit_entity_store_destroy(&store);
    return 0;
}

static int test_object_mode_duplicate(void) {
    edit_entity_store_t store;
    ASSERT_TRUE(edit_entity_store_init(&store, 16));
    uint32_t id = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_SPHERE);
    edit_entity_t *e = edit_entity_store_get_mut(&store, id);
    e->pos[0] = 5.0f;
    e->pos[1] = 3.0f;
    snprintf(e->name, sizeof(e->name), "MySphere");

    edit_selection_t sel;
    ASSERT_TRUE(edit_selection_init(&sel));
    edit_selection_add(&sel, id);

    /* Duplicate selected entities. Selection should update to new entities. */
    object_mode_duplicate(&store, &sel);

    /* Original should still exist. */
    ASSERT_TRUE(edit_entity_store_get(&store, id) != NULL);

    /* Selection should now contain the duplicate(s). */
    ASSERT_TRUE(edit_selection_count(&sel) == 1);
    const uint32_t *ids = edit_selection_ids(&sel);
    ASSERT_TRUE(ids[0] != id);

    /* Duplicate should have same position. */
    const edit_entity_t *dup = edit_entity_store_get(&store, ids[0]);
    ASSERT_TRUE(dup != NULL);
    ASSERT_FLOAT_NEAR(5.0f, dup->pos[0], 0.001f);
    ASSERT_FLOAT_NEAR(3.0f, dup->pos[1], 0.001f);

    edit_selection_destroy(&sel);
    edit_entity_store_destroy(&store);
    return 0;
}

static int test_object_mode_delete(void) {
    edit_entity_store_t store;
    ASSERT_TRUE(edit_entity_store_init(&store, 16));
    uint32_t id = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_BOX);

    edit_selection_t sel;
    ASSERT_TRUE(edit_selection_init(&sel));
    edit_selection_add(&sel, id);

    object_mode_delete(&store, &sel);

    /* Entity should be removed. */
    const edit_entity_t *e = edit_entity_store_get(&store, id);
    ASSERT_TRUE(e == NULL);

    /* Selection should be empty. */
    ASSERT_TRUE(edit_selection_count(&sel) == 0);

    edit_selection_destroy(&sel);
    edit_entity_store_destroy(&store);
    return 0;
}

static int test_object_mode_translate_empty_selection(void) {
    edit_entity_store_t store;
    ASSERT_TRUE(edit_entity_store_init(&store, 16));

    edit_selection_t sel;
    ASSERT_TRUE(edit_selection_init(&sel));

    /* No-op on empty selection. */
    float delta[3] = {1.0f, 1.0f, 1.0f};
    object_mode_translate(&store, &sel, delta);

    edit_selection_destroy(&sel);
    edit_entity_store_destroy(&store);
    return 0;
}

static int test_object_mode_multi_select_translate(void) {
    edit_entity_store_t store;
    ASSERT_TRUE(edit_entity_store_init(&store, 16));
    uint32_t id0 = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_BOX);
    uint32_t id1 = edit_entity_store_create(&store, EDIT_ENTITY_TYPE_SPHERE);

    edit_selection_t sel;
    ASSERT_TRUE(edit_selection_init(&sel));
    edit_selection_add(&sel, id0);
    edit_selection_add(&sel, id1);

    float delta[3] = {1.0f, 2.0f, 3.0f};
    object_mode_translate(&store, &sel, delta);

    const edit_entity_t *e0 = edit_entity_store_get(&store, id0);
    const edit_entity_t *e1 = edit_entity_store_get(&store, id1);
    ASSERT_FLOAT_NEAR(1.0f, e0->pos[0], 0.001f);
    ASSERT_FLOAT_NEAR(2.0f, e0->pos[1], 0.001f);
    ASSERT_FLOAT_NEAR(1.0f, e1->pos[0], 0.001f);
    ASSERT_FLOAT_NEAR(2.0f, e1->pos[1], 0.001f);

    edit_selection_destroy(&sel);
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
    {"mode_manager_init",              test_mode_manager_init},
    {"mode_manager_get_active_name",   test_mode_manager_get_active_name},
    {"mode_manager_switch",            test_mode_manager_switch},
    {"object_mode_translate",          test_object_mode_translate},
    {"object_mode_rotate",             test_object_mode_rotate},
    {"object_mode_scale",              test_object_mode_scale},
    {"object_mode_duplicate",          test_object_mode_duplicate},
    {"object_mode_delete",             test_object_mode_delete},
    {"object_mode_translate_empty",    test_object_mode_translate_empty_selection},
    {"object_mode_multi_translate",    test_object_mode_multi_select_translate},
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
