/**
 * @file mesh_inset_tests.c
 * @brief Tests for face inset and outset operations.
 */
#include <stdio.h>
#include <math.h>
#include "ferrum/editor/mesh/mesh_inset.h"
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

static bool indices_valid_(const mesh_slot_t *slot) {
    for (uint32_t i = 0; i < slot->index_count; i++) {
        if (slot->indices[i] >= slot->vertex_count) return false;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Inset single triangle                                               */
/* ------------------------------------------------------------------ */

static void test_inset_single_tri(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 32, 96);

    float n[3] = {0, 0, 1};
    mesh_slot_add_vertex(&slot, (float[3]){0,0,0}, n);
    mesh_slot_add_vertex(&slot, (float[3]){1,0,0}, n);
    mesh_slot_add_vertex(&slot, (float[3]){0,1,0}, n);
    mesh_slot_add_triangle(&slot, 0, 1, 2, 0);

    mesh_sel_bitset_t sel;
    mesh_sel_bitset_init(&sel);
    mesh_sel_bitset_set(&sel, 0);

    bool ok = mesh_inset(&slot, &sel, 0.1f, 0.0f);
    ASSERT(ok, "inset succeeded");

    /* Original tri → 1 center inset tri + 3 border quads (6 tris) = 7 tris */
    ASSERT(slot.index_count / 3 == 7, "7 faces after inset");
    ASSERT(indices_valid_(&slot), "indices valid");
    ASSERT(slot.vertex_count == 6, "3 new inset vertices");

    mesh_sel_bitset_destroy(&sel);
    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Inset with depth                                                    */
/* ------------------------------------------------------------------ */

static void test_inset_with_depth(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 32, 128);

    float n[3] = {0, 0, 1};
    mesh_slot_add_vertex(&slot, (float[3]){0,0,0}, n);
    mesh_slot_add_vertex(&slot, (float[3]){1,0,0}, n);
    mesh_slot_add_vertex(&slot, (float[3]){0,1,0}, n);
    mesh_slot_add_triangle(&slot, 0, 1, 2, 0);

    mesh_sel_bitset_t sel;
    mesh_sel_bitset_init(&sel);
    mesh_sel_bitset_set(&sel, 0);

    bool ok = mesh_inset(&slot, &sel, 0.1f, 0.5f);
    ASSERT(ok, "inset with depth succeeded");

    /* Check that inset vertices are offset along Z */
    bool found_offset = false;
    for (uint32_t i = 3; i < slot.vertex_count; i++) {
        if (fabsf(slot.positions[i*3+2] - 0.5f) < 0.01f) {
            found_offset = true;
            break;
        }
    }
    ASSERT(found_offset, "inset vertices offset by depth");

    mesh_sel_bitset_destroy(&sel);
    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Inset box face                                                      */
/* ------------------------------------------------------------------ */

static void test_inset_box_face(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 256, 1024);
    mesh_prim_box(&slot, (float[3]){1,1,1}, (uint32_t[3]){1,1,1}, (float[3]){0,0,0});

    uint32_t orig_faces = slot.index_count / 3; /* 12 */

    mesh_sel_bitset_t sel;
    mesh_sel_bitset_init(&sel);
    /* Select top face (2 triangles) */
    mesh_sel_bitset_set(&sel, 0);
    mesh_sel_bitset_set(&sel, 1);

    bool ok = mesh_inset(&slot, &sel, 0.1f, 0.0f);
    ASSERT(ok, "box inset succeeded");
    ASSERT(slot.index_count / 3 > orig_faces, "faces added");
    ASSERT(indices_valid_(&slot), "indices valid");

    mesh_sel_bitset_destroy(&sel);
    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Outset                                                              */
/* ------------------------------------------------------------------ */

static void test_outset(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 32, 96);

    float n[3] = {0, 0, 1};
    mesh_slot_add_vertex(&slot, (float[3]){0,0,0}, n);
    mesh_slot_add_vertex(&slot, (float[3]){2,0,0}, n);
    mesh_slot_add_vertex(&slot, (float[3]){1,2,0}, n);
    mesh_slot_add_triangle(&slot, 0, 1, 2, 0);

    mesh_sel_bitset_t sel;
    mesh_sel_bitset_init(&sel);
    mesh_sel_bitset_set(&sel, 0);

    bool ok = mesh_outset(&slot, &sel, 0.5f);
    ASSERT(ok, "outset succeeded");

    /* Same face count (outset modifies existing vertices in-place) */
    ASSERT(slot.index_count / 3 == 1, "same face count");

    /* Vertices should be moved outward from centroid */
    /* Centroid was (1, 0.666, 0), vertices should be farther from it */
    float cx = (slot.positions[0] + slot.positions[3] + slot.positions[6]) / 3;
    float cy = (slot.positions[1] + slot.positions[4] + slot.positions[7]) / 3;
    ASSERT(fabsf(cx - 1.0f) < 0.01f, "centroid X unchanged");
    ASSERT(cy > 0.5f && cy < 0.8f, "centroid Y reasonable");

    mesh_sel_bitset_destroy(&sel);
    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Empty selection                                                     */
/* ------------------------------------------------------------------ */

static void test_empty_sel(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 8, 24);
    float n[3] = {0,0,1};
    mesh_slot_add_vertex(&slot, (float[3]){0,0,0}, n);
    mesh_slot_add_vertex(&slot, (float[3]){1,0,0}, n);
    mesh_slot_add_vertex(&slot, (float[3]){0,1,0}, n);
    mesh_slot_add_triangle(&slot, 0, 1, 2, 0);

    mesh_sel_bitset_t sel;
    mesh_sel_bitset_init(&sel);

    ASSERT(!mesh_inset(&slot, &sel, 0.1f, 0), "empty inset returns false");
    ASSERT(!mesh_outset(&slot, &sel, 0.1f), "empty outset returns false");

    mesh_sel_bitset_destroy(&sel);
    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Null safety                                                         */
/* ------------------------------------------------------------------ */

static void test_null_safety(void) {
    ASSERT(!mesh_inset(NULL, NULL, 0, 0), "null inset");
    ASSERT(!mesh_outset(NULL, NULL, 0), "null outset");
    g_pass++;
}

int main(void) {
    printf("mesh_inset_tests:\n");
    test_inset_single_tri();
    test_inset_with_depth();
    test_inset_box_face();
    test_outset();
    test_empty_sel();
    test_null_safety();
    printf("  %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
