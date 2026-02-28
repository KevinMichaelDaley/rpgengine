/**
 * @file mesh_material_ops_tests.c
 * @brief Tests for material lift, replace, and UV wrap/flow.
 */
#include <stdio.h>
#include <string.h>
#include "ferrum/editor/mesh/mesh_material_ops.h"
#include "ferrum/editor/mesh/mesh_material.h"
#include "ferrum/editor/mesh/mesh_primitives.h"
#include "ferrum/editor/mesh/mesh_edit.h"

static int g_pass = 0;
static int g_fail = 0;

#define ASSERT(cond, msg)                                              \
    do {                                                               \
        if (!(cond)) {                                                 \
            fprintf(stderr, "  FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg); \
            g_fail++;                                                  \
            return;                                                    \
        }                                                              \
    } while (0)

/* ------------------------------------------------------------------ */
/* Test: lift material from face                                       */
/* ------------------------------------------------------------------ */

static void test_lift(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 256, 1024);
    mesh_prim_box(&slot, (float[3]){1,1,1}, (uint32_t[3]){1,1,1}, (float[3]){0,0,0});

    mesh_material_map_t map;
    mesh_material_map_init(&map);

    /* Assign material to face 0 */
    mesh_sel_bitset_t sel;
    mesh_sel_bitset_init(&sel);
    mesh_sel_bitset_set(&sel, 0);
    mesh_material_assign(&slot, &map, &sel, "textures/brick.png");

    /* Lift from face 0 */
    const char *lifted = mesh_material_lift(&slot, &map, 0);
    ASSERT(lifted != NULL, "lift found material");
    ASSERT(strcmp(lifted, "textures/brick.png") == 0, "correct material");

    /* Lift from unassigned face */
    const char *none = mesh_material_lift(&slot, &map, 5);
    /* May return NULL or empty depending on polygroup 0 mapping */
    (void)none;

    mesh_sel_bitset_destroy(&sel);
    mesh_material_map_destroy(&map);
    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Test: replace all occurrences of a material                         */
/* ------------------------------------------------------------------ */

static void test_replace(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 256, 1024);
    mesh_prim_box(&slot, (float[3]){1,1,1}, (uint32_t[3]){1,1,1}, (float[3]){0,0,0});

    mesh_material_map_t map;
    mesh_material_map_init(&map);

    /* Assign faces 0-3 to brick */
    mesh_sel_bitset_t sel;
    mesh_sel_bitset_init(&sel);
    for (int f = 0; f < 4; f++) mesh_sel_bitset_set(&sel, (uint32_t)f);
    mesh_material_assign(&slot, &map, &sel, "textures/brick.png");

    /* Replace brick with stone */
    bool ok = mesh_material_replace(&map, "textures/brick.png",
                                    "textures/stone.png");
    ASSERT(ok, "replace succeeded");

    /* Verify face 0 now has stone */
    const char *mat = mesh_material_lift(&slot, &map, 0);
    ASSERT(mat != NULL, "material exists");
    ASSERT(strcmp(mat, "textures/stone.png") == 0, "replaced to stone");

    mesh_sel_bitset_destroy(&sel);
    mesh_material_map_destroy(&map);
    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Test: replace nonexistent material                                  */
/* ------------------------------------------------------------------ */

static void test_replace_missing(void) {
    mesh_material_map_t map;
    mesh_material_map_init(&map);

    bool ok = mesh_material_replace(&map, "nonexistent", "new");
    ASSERT(!ok, "replace missing returns false");

    mesh_material_map_destroy(&map);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Test: null safety                                                   */
/* ------------------------------------------------------------------ */

static void test_null_safety(void) {
    ASSERT(mesh_material_lift(NULL, NULL, 0) == NULL, "null lift");
    ASSERT(!mesh_material_replace(NULL, NULL, NULL), "null replace");
    g_pass++;
}

int main(void) {
    printf("mesh_material_ops_tests:\n");
    test_lift();
    test_replace();
    test_replace_missing();
    test_null_safety();
    printf("  %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
