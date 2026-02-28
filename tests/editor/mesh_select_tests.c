/**
 * @file mesh_select_tests.c
 * @brief Tests for mesh element selection: by indices, select_all,
 *        invert, flood fill, grow, shrink, select_similar.
 *
 * Test mesh: a 2×2 quad grid (8 triangles, 9 vertices).
 *
 *   v0---v1---v2
 *   | \  | \  |
 *   |  \ |  \ |
 *   v3---v4---v5
 *   | \  | \  |
 *   |  \ |  \ |
 *   v6---v7---v8
 *
 *   Row 0: faces 0-3, Row 1: faces 4-7
 *   All normals (0,0,1). All polygroup 0.
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "ferrum/editor/mesh/mesh_edit.h"
#include "ferrum/editor/mesh/mesh_selection.h"
#include "ferrum/editor/mesh/mesh_select.h"

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
/* Build 2×2 quad grid                                                 */
/* ------------------------------------------------------------------ */

static void build_grid_(mesh_slot_t *slot) {
    mesh_slot_init(slot, 16, 48);
    float n[3] = {0, 0, 1};
    /* 3×3 = 9 vertices */
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) {
            float p[3] = {(float)c, (float)(2 - r), 0};
            mesh_slot_add_vertex(slot, p, n);
        }
    }
    /* 8 triangles (2 per quad, 4 quads) */
    /* Quad (r,c): tl=r*3+c, tr=tl+1, bl=(r+1)*3+c, br=bl+1 */
    for (int r = 0; r < 2; r++) {
        for (int c = 0; c < 2; c++) {
            uint32_t tl = (uint32_t)(r * 3 + c);
            uint32_t tr = tl + 1;
            uint32_t bl = tl + 3;
            uint32_t br = bl + 1;
            mesh_slot_add_triangle(slot, tl, bl, tr, 0);
            mesh_slot_add_triangle(slot, tr, bl, br, 0);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Select by indices                                                   */
/* ------------------------------------------------------------------ */

static void test_select_by_indices(void) {
    mesh_sel_bitset_t sel;
    mesh_sel_bitset_init(&sel);

    uint32_t indices[] = {0, 3, 7};
    mesh_select_by_indices(&sel, indices, 3);
    ASSERT(sel.count == 3, "3 selected");
    ASSERT(mesh_sel_bitset_test(&sel, 0), "0 selected");
    ASSERT(mesh_sel_bitset_test(&sel, 3), "3 selected");
    ASSERT(mesh_sel_bitset_test(&sel, 7), "7 selected");
    ASSERT(!mesh_sel_bitset_test(&sel, 1), "1 not selected");

    mesh_sel_bitset_destroy(&sel);
    g_pass++;
}

static void test_deselect_by_indices(void) {
    mesh_sel_bitset_t sel;
    mesh_sel_bitset_init(&sel);

    /* Select 0,1,2,3 then deselect 1,3 */
    uint32_t all[] = {0, 1, 2, 3};
    mesh_select_by_indices(&sel, all, 4);

    uint32_t rm[] = {1, 3};
    mesh_deselect_by_indices(&sel, rm, 2);
    ASSERT(sel.count == 2, "2 remaining");
    ASSERT(mesh_sel_bitset_test(&sel, 0), "0 still");
    ASSERT(!mesh_sel_bitset_test(&sel, 1), "1 removed");

    mesh_sel_bitset_destroy(&sel);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Select all / clear / invert                                         */
/* ------------------------------------------------------------------ */

static void test_select_all(void) {
    mesh_sel_bitset_t sel;
    mesh_sel_bitset_init(&sel);

    mesh_select_all(&sel, 8);
    ASSERT(sel.count == 8, "all 8 selected");
    for (uint32_t i = 0; i < 8; i++) {
        ASSERT(mesh_sel_bitset_test(&sel, i), "bit set");
    }

    mesh_sel_bitset_destroy(&sel);
    g_pass++;
}

static void test_invert(void) {
    mesh_sel_bitset_t sel;
    mesh_sel_bitset_init(&sel);

    mesh_sel_bitset_set(&sel, 0);
    mesh_sel_bitset_set(&sel, 2);

    mesh_select_invert(&sel, 4);
    ASSERT(!mesh_sel_bitset_test(&sel, 0), "0 inverted to off");
    ASSERT(mesh_sel_bitset_test(&sel, 1), "1 inverted to on");
    ASSERT(!mesh_sel_bitset_test(&sel, 2), "2 inverted to off");
    ASSERT(mesh_sel_bitset_test(&sel, 3), "3 inverted to on");
    ASSERT(sel.count == 2, "2 selected after invert");

    mesh_sel_bitset_destroy(&sel);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Flood fill                                                          */
/* ------------------------------------------------------------------ */

static void test_flood_all_connected(void) {
    mesh_slot_t slot;
    build_grid_(&slot);

    mesh_sel_bitset_t sel;
    mesh_sel_bitset_init(&sel);

    /* Flood from face 0 should reach all 8 faces (all connected) */
    mesh_select_flood(&slot, &sel, 0);
    ASSERT(sel.count == 8, "all 8 faces reached by flood");

    mesh_sel_bitset_destroy(&sel);
    mesh_slot_destroy(&slot);
    g_pass++;
}

static void test_flood_single_tri(void) {
    /* Single disconnected triangle */
    mesh_slot_t slot;
    mesh_slot_init(&slot, 4, 12);
    float n[3] = {0,0,1};
    float p0[3]={0,0,0}, p1[3]={1,0,0}, p2[3]={0,1,0};
    mesh_slot_add_vertex(&slot, p0, n);
    mesh_slot_add_vertex(&slot, p1, n);
    mesh_slot_add_vertex(&slot, p2, n);
    mesh_slot_add_triangle(&slot, 0, 1, 2, 0);

    mesh_sel_bitset_t sel;
    mesh_sel_bitset_init(&sel);
    mesh_select_flood(&slot, &sel, 0);
    ASSERT(sel.count == 1, "single face");

    mesh_sel_bitset_destroy(&sel);
    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Grow / Shrink selection                                             */
/* ------------------------------------------------------------------ */

static void test_grow_once(void) {
    mesh_slot_t slot;
    build_grid_(&slot);

    mesh_sel_bitset_t sel;
    mesh_sel_bitset_init(&sel);

    /* Start with face 0 selected, grow by 1 */
    mesh_sel_bitset_set(&sel, 0);
    mesh_select_grow(&slot, &sel, 1);

    /* Face 0 shares edges with faces 1, 2 (depending on topology).
     * At minimum, face 0 itself + adjacent faces should be selected. */
    ASSERT(sel.count > 1, "grew beyond 1");
    ASSERT(mesh_sel_bitset_test(&sel, 0), "original still selected");

    mesh_sel_bitset_destroy(&sel);
    mesh_slot_destroy(&slot);
    g_pass++;
}

static void test_shrink_once(void) {
    mesh_slot_t slot;
    build_grid_(&slot);

    mesh_sel_bitset_t sel;
    mesh_sel_bitset_init(&sel);

    /* Select all 8, shrink by 1 = removes boundary faces */
    mesh_select_all(&sel, 8);
    mesh_select_shrink(&slot, &sel, 1);

    /* After shrinking, boundary faces should be deselected.
     * Interior faces (if any) remain. For a 2×2 grid all faces
     * touch the boundary, so all may be deselected. */
    ASSERT(sel.count < 8, "shrank from 8");

    mesh_sel_bitset_destroy(&sel);
    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Select similar (by normal)                                          */
/* ------------------------------------------------------------------ */

static void test_select_similar_normal(void) {
    mesh_slot_t slot;
    build_grid_(&slot);

    mesh_sel_bitset_t sel;
    mesh_sel_bitset_init(&sel);

    /* All faces have normal (0,0,1), so selecting similar to face 0 with
     * small threshold should select all faces */
    float ref_normal[3] = {0, 0, 1};
    mesh_select_similar_normal(&slot, &sel, ref_normal, 5.0f);
    ASSERT(sel.count == 8, "all faces have same normal");

    mesh_sel_bitset_destroy(&sel);
    mesh_slot_destroy(&slot);
    g_pass++;
}

static void test_select_similar_normal_miss(void) {
    mesh_slot_t slot;
    build_grid_(&slot);

    mesh_sel_bitset_t sel;
    mesh_sel_bitset_init(&sel);

    /* No face has normal (1,0,0) */
    float ref_normal[3] = {1, 0, 0};
    mesh_select_similar_normal(&slot, &sel, ref_normal, 5.0f);
    ASSERT(sel.count == 0, "no faces match perpendicular normal");

    mesh_sel_bitset_destroy(&sel);
    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Null safety                                                         */
/* ------------------------------------------------------------------ */

static void test_null_safety(void) {
    mesh_select_by_indices(NULL, NULL, 0);
    mesh_deselect_by_indices(NULL, NULL, 0);
    mesh_select_all(NULL, 0);
    mesh_select_invert(NULL, 0);
    mesh_select_flood(NULL, NULL, 0);
    mesh_select_grow(NULL, NULL, 0);
    mesh_select_shrink(NULL, NULL, 0);
    mesh_select_similar_normal(NULL, NULL, NULL, 0);
    /* No crash = pass */
    g_pass++;
}

/* ================================================================== */
/* Main                                                                */
/* ================================================================== */

int main(void) {
    printf("mesh_select_tests:\n");

    test_select_by_indices();
    test_deselect_by_indices();
    test_select_all();
    test_invert();
    test_flood_all_connected();
    test_flood_single_tri();
    test_grow_once();
    test_shrink_once();
    test_select_similar_normal();
    test_select_similar_normal_miss();
    test_null_safety();

    printf("  %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
