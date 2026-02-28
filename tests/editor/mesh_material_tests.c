/**
 * @file mesh_material_tests.c
 * @brief Tests for per-face material assignment via polygroup mapping.
 */
#include <stdio.h>
#include <string.h>
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
/* Test: assign material to single face                                */
/* ------------------------------------------------------------------ */

static void test_assign_single(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 8, 24);

    float n[3] = {0,0,1};
    mesh_slot_add_vertex(&slot, (float[3]){0,0,0}, n);
    mesh_slot_add_vertex(&slot, (float[3]){1,0,0}, n);
    mesh_slot_add_vertex(&slot, (float[3]){1,1,0}, n);
    mesh_slot_add_triangle(&slot, 0, 1, 2, 0);

    mesh_material_map_t map;
    mesh_material_map_init(&map);

    mesh_sel_bitset_t sel;
    mesh_sel_bitset_init(&sel);
    mesh_sel_bitset_set(&sel, 0);

    bool ok = mesh_material_assign(&slot, &map, &sel, "textures/brick.png");
    ASSERT(ok, "assign succeeded");

    /* Face 0 should map to the material */
    uint16_t pg = slot.polygroup_ids[0];
    const char *mat = mesh_material_map_get(&map, pg);
    ASSERT(mat != NULL, "mapping exists");
    ASSERT(strcmp(mat, "textures/brick.png") == 0, "correct material path");

    mesh_sel_bitset_destroy(&sel);
    mesh_material_map_destroy(&map);
    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Test: assign multiple materials to different faces                   */
/* ------------------------------------------------------------------ */

static void test_assign_multiple(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 256, 1024);
    mesh_prim_box(&slot, (float[3]){1,1,1}, (uint32_t[3]){1,1,1}, (float[3]){0,0,0});

    mesh_material_map_t map;
    mesh_material_map_init(&map);

    uint32_t fc = slot.index_count / 3;
    ASSERT(fc >= 4, "box has faces");

    /* Assign first 2 faces to brick */
    mesh_sel_bitset_t sel1;
    mesh_sel_bitset_init(&sel1);
    mesh_sel_bitset_set(&sel1, 0);
    mesh_sel_bitset_set(&sel1, 1);
    mesh_material_assign(&slot, &map, &sel1, "textures/brick.png");

    /* Assign faces 2-3 to wood */
    mesh_sel_bitset_t sel2;
    mesh_sel_bitset_init(&sel2);
    mesh_sel_bitset_set(&sel2, 2);
    mesh_sel_bitset_set(&sel2, 3);
    mesh_material_assign(&slot, &map, &sel2, "textures/wood.png");

    /* Verify different polygroups and correct materials */
    uint16_t pg0 = slot.polygroup_ids[0];
    uint16_t pg2 = slot.polygroup_ids[2];
    ASSERT(pg0 != pg2, "different polygroups");

    const char *m0 = mesh_material_map_get(&map, pg0);
    const char *m2 = mesh_material_map_get(&map, pg2);
    ASSERT(m0 && strcmp(m0, "textures/brick.png") == 0, "brick material");
    ASSERT(m2 && strcmp(m2, "textures/wood.png") == 0, "wood material");

    mesh_sel_bitset_destroy(&sel1);
    mesh_sel_bitset_destroy(&sel2);
    mesh_material_map_destroy(&map);
    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Test: reassign reuses existing polygroup                            */
/* ------------------------------------------------------------------ */

static void test_reassign_reuse(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 8, 48);

    float n[3] = {0,0,1};
    mesh_slot_add_vertex(&slot, (float[3]){0,0,0}, n);
    mesh_slot_add_vertex(&slot, (float[3]){1,0,0}, n);
    mesh_slot_add_vertex(&slot, (float[3]){1,1,0}, n);
    mesh_slot_add_vertex(&slot, (float[3]){0,1,0}, n);
    mesh_slot_add_triangle(&slot, 0, 1, 2, 0);
    mesh_slot_add_triangle(&slot, 0, 2, 3, 0);

    mesh_material_map_t map;
    mesh_material_map_init(&map);

    /* Assign face 0 to brick */
    mesh_sel_bitset_t sel0;
    mesh_sel_bitset_init(&sel0);
    mesh_sel_bitset_set(&sel0, 0);
    mesh_material_assign(&slot, &map, &sel0, "textures/brick.png");
    uint16_t pg_first = slot.polygroup_ids[0];

    /* Assign face 1 to same material — should reuse polygroup */
    mesh_sel_bitset_t sel1;
    mesh_sel_bitset_init(&sel1);
    mesh_sel_bitset_set(&sel1, 1);
    mesh_material_assign(&slot, &map, &sel1, "textures/brick.png");
    uint16_t pg_second = slot.polygroup_ids[1];

    ASSERT(pg_first == pg_second, "same polygroup for same material");

    mesh_sel_bitset_destroy(&sel0);
    mesh_sel_bitset_destroy(&sel1);
    mesh_material_map_destroy(&map);
    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Test: get material for face                                         */
/* ------------------------------------------------------------------ */

static void test_get_face_material(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 8, 24);

    float n[3] = {0,0,1};
    mesh_slot_add_vertex(&slot, (float[3]){0,0,0}, n);
    mesh_slot_add_vertex(&slot, (float[3]){1,0,0}, n);
    mesh_slot_add_vertex(&slot, (float[3]){1,1,0}, n);
    mesh_slot_add_triangle(&slot, 0, 1, 2, 0);

    mesh_material_map_t map;
    mesh_material_map_init(&map);

    mesh_sel_bitset_t sel;
    mesh_sel_bitset_init(&sel);
    mesh_sel_bitset_set(&sel, 0);
    mesh_material_assign(&slot, &map, &sel, "textures/stone.png");

    /* Convenience: get material path for a specific face */
    const char *mat = mesh_material_get_face(&slot, &map, 0);
    ASSERT(mat != NULL, "face material found");
    ASSERT(strcmp(mat, "textures/stone.png") == 0, "correct face material");

    /* Out-of-bounds face */
    const char *bad = mesh_material_get_face(&slot, &map, 999);
    ASSERT(bad == NULL, "out-of-bounds returns NULL");

    mesh_sel_bitset_destroy(&sel);
    mesh_material_map_destroy(&map);
    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Test: null safety                                                   */
/* ------------------------------------------------------------------ */

static void test_null_safety(void) {
    ASSERT(!mesh_material_assign(NULL, NULL, NULL, NULL), "null assign");
    ASSERT(mesh_material_get_face(NULL, NULL, 0) == NULL, "null get_face");
    ASSERT(mesh_material_map_get(NULL, 0) == NULL, "null map_get");
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Test: map count                                                     */
/* ------------------------------------------------------------------ */

static void test_map_count(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 256, 1024);
    mesh_prim_box(&slot, (float[3]){1,1,1}, (uint32_t[3]){1,1,1}, (float[3]){0,0,0});

    mesh_material_map_t map;
    mesh_material_map_init(&map);

    mesh_sel_bitset_t sel;
    mesh_sel_bitset_init(&sel);

    /* Assign 3 different materials */
    mesh_sel_bitset_set(&sel, 0);
    mesh_material_assign(&slot, &map, &sel, "mat_a");

    mesh_sel_bitset_clear_all(&sel);
    mesh_sel_bitset_set(&sel, 1);
    mesh_material_assign(&slot, &map, &sel, "mat_b");

    mesh_sel_bitset_clear_all(&sel);
    mesh_sel_bitset_set(&sel, 2);
    mesh_material_assign(&slot, &map, &sel, "mat_c");

    ASSERT(mesh_material_map_count(&map) == 3, "3 materials assigned");

    /* Reassign face to existing material — no new entry */
    mesh_sel_bitset_clear_all(&sel);
    mesh_sel_bitset_set(&sel, 3);
    mesh_material_assign(&slot, &map, &sel, "mat_a");
    ASSERT(mesh_material_map_count(&map) == 3, "still 3 after reuse");

    mesh_sel_bitset_destroy(&sel);
    mesh_material_map_destroy(&map);
    mesh_slot_destroy(&slot);
    g_pass++;
}

int main(void) {
    printf("mesh_material_tests:\n");
    test_assign_single();
    test_assign_multiple();
    test_reassign_reuse();
    test_get_face_material();
    test_null_safety();
    test_map_count();
    printf("  %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
