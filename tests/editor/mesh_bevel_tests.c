/**
 * @file mesh_bevel_tests.c
 * @brief Tests for edge bevel and vertex bevel.
 */
#include <stdio.h>
#include <math.h>
#include "ferrum/editor/mesh/mesh_bevel.h"
#include "ferrum/editor/mesh/mesh_primitives.h"
#include "ferrum/editor/mesh/mesh_edit.h"
#include "ferrum/editor/mesh/mesh_selection.h"

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
/* Edge bevel on simple quad                                           */
/* ------------------------------------------------------------------ */

static void test_edge_bevel_quad(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 64, 256);

    /* 2-triangle quad: (0,0,0) (1,0,0) (1,1,0) (0,1,0) */
    float n[3] = {0, 0, 1};
    mesh_slot_add_vertex(&slot, (float[3]){0,0,0}, n);
    mesh_slot_add_vertex(&slot, (float[3]){1,0,0}, n);
    mesh_slot_add_vertex(&slot, (float[3]){1,1,0}, n);
    mesh_slot_add_vertex(&slot, (float[3]){0,1,0}, n);
    mesh_slot_add_triangle(&slot, 0, 1, 2, 0);
    mesh_slot_add_triangle(&slot, 0, 2, 3, 0);

    /* Build edge table to find shared edge (0,2) */
    mesh_edge_table_t et;
    mesh_edge_table_build(&et, &slot);

    /* Select the diagonal edge (0,2) → canonical (0,2) */
    mesh_sel_bitset_t sel;
    mesh_sel_bitset_init(&sel);
    uint32_t ei = mesh_edge_table_find(&et, 0, 2);
    if (ei != UINT32_MAX) {
        mesh_sel_bitset_set(&sel, ei);
    }

    bool ok = mesh_bevel_edges(&slot, &sel, &et, 0.1f, 1);
    ASSERT(ok, "edge bevel succeeded");
    ASSERT(indices_valid_(&slot), "indices valid");
    ASSERT(slot.vertex_count > 4, "new vertices created");
    ASSERT(slot.index_count / 3 > 2, "new faces created");

    mesh_sel_bitset_destroy(&sel);
    mesh_edge_table_destroy(&et);
    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Vertex bevel on single triangle                                     */
/* ------------------------------------------------------------------ */

static void test_vertex_bevel(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 64, 256);

    float n[3] = {0, 0, 1};
    mesh_slot_add_vertex(&slot, (float[3]){0,0,0}, n);
    mesh_slot_add_vertex(&slot, (float[3]){2,0,0}, n);
    mesh_slot_add_vertex(&slot, (float[3]){1,2,0}, n);
    mesh_slot_add_triangle(&slot, 0, 1, 2, 0);

    /* Select vertex 0 */
    mesh_sel_bitset_t sel;
    mesh_sel_bitset_init(&sel);
    mesh_sel_bitset_set(&sel, 0);

    bool ok = mesh_bevel_vertices(&slot, &sel, 0.2f);
    ASSERT(ok, "vertex bevel succeeded");
    ASSERT(indices_valid_(&slot), "indices valid");
    ASSERT(slot.vertex_count > 3, "new vertices from bevel");

    mesh_sel_bitset_destroy(&sel);
    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Empty selection                                                     */
/* ------------------------------------------------------------------ */

static void test_empty_edge_bevel(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 8, 24);
    float n[3] = {0,0,1};
    mesh_slot_add_vertex(&slot, (float[3]){0,0,0}, n);
    mesh_slot_add_vertex(&slot, (float[3]){1,0,0}, n);
    mesh_slot_add_vertex(&slot, (float[3]){0,1,0}, n);
    mesh_slot_add_triangle(&slot, 0, 1, 2, 0);

    mesh_edge_table_t et;
    mesh_edge_table_build(&et, &slot);

    mesh_sel_bitset_t sel;
    mesh_sel_bitset_init(&sel);

    ASSERT(!mesh_bevel_edges(&slot, &sel, &et, 0.1f, 1), "empty bevel");

    mesh_sel_bitset_destroy(&sel);
    mesh_edge_table_destroy(&et);
    mesh_slot_destroy(&slot);
    g_pass++;
}

static void test_empty_vertex_bevel(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 8, 24);
    float n[3] = {0,0,1};
    mesh_slot_add_vertex(&slot, (float[3]){0,0,0}, n);
    mesh_slot_add_vertex(&slot, (float[3]){1,0,0}, n);
    mesh_slot_add_vertex(&slot, (float[3]){0,1,0}, n);
    mesh_slot_add_triangle(&slot, 0, 1, 2, 0);

    mesh_sel_bitset_t sel;
    mesh_sel_bitset_init(&sel);

    ASSERT(!mesh_bevel_vertices(&slot, &sel, 0.1f), "empty vertex bevel");

    mesh_sel_bitset_destroy(&sel);
    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Null safety                                                         */
/* ------------------------------------------------------------------ */

static void test_null_safety(void) {
    ASSERT(!mesh_bevel_edges(NULL, NULL, NULL, 0, 1), "null edge bevel");
    ASSERT(!mesh_bevel_vertices(NULL, NULL, 0), "null vertex bevel");
    g_pass++;
}

int main(void) {
    printf("mesh_bevel_tests:\n");
    test_edge_bevel_quad();
    test_vertex_bevel();
    test_empty_edge_bevel();
    test_empty_vertex_bevel();
    test_null_safety();
    printf("  %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
