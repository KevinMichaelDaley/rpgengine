/**
 * @file bone_selection_tests.c
 * @brief Tests for bone selection state.
 *
 * Validates the edit_bone_selection_t type which tracks
 * (entity_id, bone_index) pairs for skeleton bone selection.
 */

#include "ferrum/editor/edit_bone_selection.h"

#include <stdio.h>
#include <string.h>

static int g_pass, g_fail;

#define ASSERT(cond) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; \
           fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

/* ---- Tests ---- */

/** Init produces an empty selection. */
static void test_init(void) {
    edit_bone_selection_t sel;
    edit_bone_selection_init(&sel);
    ASSERT(edit_bone_selection_count(&sel) == 0);
    ASSERT(sel.entity_id == EDIT_BONE_SEL_NONE);
    edit_bone_selection_destroy(&sel);
}

/** Add and query a single bone. */
static void test_add_one(void) {
    edit_bone_selection_t sel;
    edit_bone_selection_init(&sel);

    ASSERT(edit_bone_selection_add(&sel, 42, 5));
    ASSERT(edit_bone_selection_count(&sel) == 1);
    ASSERT(edit_bone_selection_contains(&sel, 42, 5));
    ASSERT(!edit_bone_selection_contains(&sel, 42, 6));
    ASSERT(sel.entity_id == 42);

    edit_bone_selection_destroy(&sel);
}

/** Adding bones from different entities clears the previous selection. */
static void test_entity_switch_clears(void) {
    edit_bone_selection_t sel;
    edit_bone_selection_init(&sel);

    edit_bone_selection_add(&sel, 1, 0);
    edit_bone_selection_add(&sel, 1, 1);
    ASSERT(edit_bone_selection_count(&sel) == 2);

    /* Switch to a different entity — previous selection cleared. */
    edit_bone_selection_add(&sel, 2, 0);
    ASSERT(edit_bone_selection_count(&sel) == 1);
    ASSERT(!edit_bone_selection_contains(&sel, 1, 0));
    ASSERT(edit_bone_selection_contains(&sel, 2, 0));
    ASSERT(sel.entity_id == 2);

    edit_bone_selection_destroy(&sel);
}

/** Multi-select bones on same entity. */
static void test_multi_select(void) {
    edit_bone_selection_t sel;
    edit_bone_selection_init(&sel);

    edit_bone_selection_add(&sel, 10, 0);
    edit_bone_selection_add(&sel, 10, 3);
    edit_bone_selection_add(&sel, 10, 7);
    ASSERT(edit_bone_selection_count(&sel) == 3);
    ASSERT(edit_bone_selection_contains(&sel, 10, 0));
    ASSERT(edit_bone_selection_contains(&sel, 10, 3));
    ASSERT(edit_bone_selection_contains(&sel, 10, 7));

    edit_bone_selection_destroy(&sel);
}

/** Remove a bone from selection. */
static void test_remove(void) {
    edit_bone_selection_t sel;
    edit_bone_selection_init(&sel);

    edit_bone_selection_add(&sel, 5, 0);
    edit_bone_selection_add(&sel, 5, 1);
    ASSERT(edit_bone_selection_count(&sel) == 2);

    ASSERT(edit_bone_selection_remove(&sel, 5, 0));
    ASSERT(edit_bone_selection_count(&sel) == 1);
    ASSERT(!edit_bone_selection_contains(&sel, 5, 0));
    ASSERT(edit_bone_selection_contains(&sel, 5, 1));

    /* Remove non-existent bone. */
    ASSERT(!edit_bone_selection_remove(&sel, 5, 99));

    edit_bone_selection_destroy(&sel);
}

/** Toggle bone selection. */
static void test_toggle(void) {
    edit_bone_selection_t sel;
    edit_bone_selection_init(&sel);

    /* Toggle on. */
    edit_bone_selection_toggle(&sel, 3, 2);
    ASSERT(edit_bone_selection_contains(&sel, 3, 2));

    /* Toggle off. */
    edit_bone_selection_toggle(&sel, 3, 2);
    ASSERT(!edit_bone_selection_contains(&sel, 3, 2));
    ASSERT(edit_bone_selection_count(&sel) == 0);

    edit_bone_selection_destroy(&sel);
}

/** Clear empties everything. */
static void test_clear(void) {
    edit_bone_selection_t sel;
    edit_bone_selection_init(&sel);

    edit_bone_selection_add(&sel, 1, 0);
    edit_bone_selection_add(&sel, 1, 1);
    edit_bone_selection_clear(&sel);
    ASSERT(edit_bone_selection_count(&sel) == 0);
    ASSERT(sel.entity_id == EDIT_BONE_SEL_NONE);

    edit_bone_selection_destroy(&sel);
}

/** Duplicate add is a no-op. */
static void test_duplicate_add(void) {
    edit_bone_selection_t sel;
    edit_bone_selection_init(&sel);

    edit_bone_selection_add(&sel, 1, 5);
    edit_bone_selection_add(&sel, 1, 5);
    ASSERT(edit_bone_selection_count(&sel) == 1);

    edit_bone_selection_destroy(&sel);
}

/** Get selected bone indices array. */
static void test_get_bones(void) {
    edit_bone_selection_t sel;
    edit_bone_selection_init(&sel);

    edit_bone_selection_add(&sel, 7, 2);
    edit_bone_selection_add(&sel, 7, 0);
    edit_bone_selection_add(&sel, 7, 4);

    uint32_t count = 0;
    const uint32_t *bones = edit_bone_selection_bones(&sel, &count);
    ASSERT(count == 3);
    ASSERT(bones != NULL);

    /* Bones should be present (order may vary). */
    bool found[3] = {false, false, false};
    for (uint32_t i = 0; i < count; i++) {
        if (bones[i] == 0) found[0] = true;
        if (bones[i] == 2) found[1] = true;
        if (bones[i] == 4) found[2] = true;
    }
    ASSERT(found[0]);
    ASSERT(found[1]);
    ASSERT(found[2]);

    edit_bone_selection_destroy(&sel);
}

/* ---- Main ---- */

int main(void) {
    printf("bone_selection_tests:\n");

    test_init();
    test_add_one();
    test_entity_switch_clears();
    test_multi_select();
    test_remove();
    test_toggle();
    test_clear();
    test_duplicate_add();
    test_get_bones();

    printf("bone_selection_tests: %d passed, %d failed\n",
           g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
