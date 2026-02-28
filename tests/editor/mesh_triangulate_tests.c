/**
 * @file mesh_triangulate_tests.c
 * @brief Tests for triangulate and tris-to-quads.
 */
#include <stdio.h>
#include "ferrum/editor/mesh/mesh_triangulate.h"
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

static void test_triangulate_noop(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 8, 24);
    float n[3] = {0,0,1};
    mesh_slot_add_vertex(&slot, (float[3]){0,0,0}, n);
    mesh_slot_add_vertex(&slot, (float[3]){1,0,0}, n);
    mesh_slot_add_vertex(&slot, (float[3]){0,1,0}, n);
    mesh_slot_add_triangle(&slot, 0, 1, 2, 0);

    mesh_sel_bitset_t sel;
    mesh_sel_bitset_init(&sel);
    mesh_sel_bitset_set(&sel, 0);

    bool ok = mesh_triangulate(&slot, &sel);
    ASSERT(ok, "triangulate no-op");
    ASSERT(slot.index_count == 3, "unchanged");

    mesh_sel_bitset_destroy(&sel);
    mesh_slot_destroy(&slot);
    g_pass++;
}

static void test_tris_to_quads_box(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 256, 1024);
    mesh_prim_box(&slot, (float[3]){1,1,1}, (uint32_t[3]){1,1,1}, (float[3]){0,0,0});

    /* Box has 12 triangles, 6 coplanar pairs */
    uint32_t pairs = mesh_tris_to_quads(&slot, 0.99f);
    ASSERT(pairs == 6, "6 coplanar pairs");

    mesh_slot_destroy(&slot);
    g_pass++;
}

static void test_tris_to_quads_single_tri(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 8, 24);
    float n[3] = {0,0,1};
    mesh_slot_add_vertex(&slot, (float[3]){0,0,0}, n);
    mesh_slot_add_vertex(&slot, (float[3]){1,0,0}, n);
    mesh_slot_add_vertex(&slot, (float[3]){0,1,0}, n);
    mesh_slot_add_triangle(&slot, 0, 1, 2, 0);

    uint32_t pairs = mesh_tris_to_quads(&slot, 0.99f);
    ASSERT(pairs == 0, "no pairs for single tri");

    mesh_slot_destroy(&slot);
    g_pass++;
}

static void test_null_safety(void) {
    ASSERT(mesh_tris_to_quads(NULL, 0) == 0, "null tris_to_quads");
    g_pass++;
}

int main(void) {
    printf("mesh_triangulate_tests:\n");
    test_triangulate_noop();
    test_tris_to_quads_box();
    test_tris_to_quads_single_tri();
    test_null_safety();
    printf("  %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
