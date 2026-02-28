/**
 * @file mesh_normals_tests.c
 * @brief Tests for detach, split, flip normals, recalculate normals.
 */
#include <stdio.h>
#include <math.h>
#include "ferrum/editor/mesh/mesh_normals.h"
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
/* Flip normals                                                        */
/* ------------------------------------------------------------------ */

static void test_flip_normals(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 8, 24);

    float n[3] = {0, 0, 1};
    mesh_slot_add_vertex(&slot, (float[3]){0,0,0}, n);
    mesh_slot_add_vertex(&slot, (float[3]){1,0,0}, n);
    mesh_slot_add_vertex(&slot, (float[3]){0,1,0}, n);
    mesh_slot_add_triangle(&slot, 0, 1, 2, 0);

    mesh_sel_bitset_t sel;
    mesh_sel_bitset_init(&sel);
    mesh_sel_bitset_set(&sel, 0);

    /* Before flip: (0, 1, 2) */
    ASSERT(slot.indices[0] == 0, "before flip i0");

    bool ok = mesh_flip_normals(&slot, &sel);
    ASSERT(ok, "flip succeeded");

    /* After flip: winding reversed (0, 2, 1) */
    ASSERT(slot.indices[1] == 2, "after flip i1");
    ASSERT(slot.indices[2] == 1, "after flip i2");

    mesh_sel_bitset_destroy(&sel);
    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Recalculate normals                                                 */
/* ------------------------------------------------------------------ */

static void test_recalc_normals(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 8, 24);

    /* Set wrong normals */
    float wrong_n[3] = {1, 0, 0};
    mesh_slot_add_vertex(&slot, (float[3]){0,0,0}, wrong_n);
    mesh_slot_add_vertex(&slot, (float[3]){1,0,0}, wrong_n);
    mesh_slot_add_vertex(&slot, (float[3]){0,1,0}, wrong_n);
    mesh_slot_add_triangle(&slot, 0, 1, 2, 0);

    bool ok = mesh_recalculate_normals(&slot);
    ASSERT(ok, "recalc succeeded");

    /* Face normal should be (0,0,1) — check all verts */
    for (uint32_t v = 0; v < 3; v++) {
        ASSERT(fabsf(slot.normals[v*3+2] - 1.0f) < 0.01f, "normal Z ~= 1");
    }

    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Split                                                               */
/* ------------------------------------------------------------------ */

static void test_split(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 16, 48);

    float n[3] = {0, 0, 1};
    /* Two triangles sharing edge (0,1): (0,1,2) and (0,3,1) */
    mesh_slot_add_vertex(&slot, (float[3]){0,0,0}, n);
    mesh_slot_add_vertex(&slot, (float[3]){1,0,0}, n);
    mesh_slot_add_vertex(&slot, (float[3]){0.5f,1,0}, n);
    mesh_slot_add_vertex(&slot, (float[3]){0.5f,-1,0}, n);
    mesh_slot_add_triangle(&slot, 0, 1, 2, 0);
    mesh_slot_add_triangle(&slot, 0, 3, 1, 0);

    mesh_sel_bitset_t sel;
    mesh_sel_bitset_init(&sel);
    mesh_sel_bitset_set(&sel, 0); /* Select first face */

    uint32_t orig_verts = slot.vertex_count;
    bool ok = mesh_split_selection(&slot, &sel);
    ASSERT(ok, "split succeeded");

    /* Shared vertices (0 and 1) should be duplicated */
    ASSERT(slot.vertex_count > orig_verts, "vertices duplicated");

    mesh_sel_bitset_destroy(&sel);
    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Detach                                                              */
/* ------------------------------------------------------------------ */

static void test_detach(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 16, 48);

    float n[3] = {0, 0, 1};
    mesh_slot_add_vertex(&slot, (float[3]){0,0,0}, n);
    mesh_slot_add_vertex(&slot, (float[3]){1,0,0}, n);
    mesh_slot_add_vertex(&slot, (float[3]){0.5f,1,0}, n);
    mesh_slot_add_vertex(&slot, (float[3]){2,0,0}, n);
    mesh_slot_add_vertex(&slot, (float[3]){1.5f,1,0}, n);
    mesh_slot_add_triangle(&slot, 0, 1, 2, 0);
    mesh_slot_add_triangle(&slot, 1, 3, 4, 0);

    mesh_sel_bitset_t sel;
    mesh_sel_bitset_init(&sel);
    mesh_sel_bitset_set(&sel, 0); /* Detach first face */

    mesh_slot_t target;
    mesh_slot_init(&target, 8, 24);

    bool ok = mesh_detach(&slot, &sel, &target);
    ASSERT(ok, "detach succeeded");

    /* Source should have 1 face, target should have 1 face */
    ASSERT(slot.index_count / 3 == 1, "source has 1 face");
    ASSERT(target.index_count / 3 == 1, "target has 1 face");

    mesh_sel_bitset_destroy(&sel);
    mesh_slot_destroy(&target);
    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Null / empty                                                        */
/* ------------------------------------------------------------------ */

static void test_null_safety(void) {
    ASSERT(!mesh_flip_normals(NULL, NULL), "null flip");
    ASSERT(!mesh_recalculate_normals(NULL), "null recalc");
    ASSERT(!mesh_split_selection(NULL, NULL), "null split");
    ASSERT(!mesh_detach(NULL, NULL, NULL), "null detach");
    g_pass++;
}

int main(void) {
    printf("mesh_normals_tests:\n");
    test_flip_normals();
    test_recalc_normals();
    test_split();
    test_detach();
    test_null_safety();
    printf("  %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
