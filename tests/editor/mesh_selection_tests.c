/**
 * @file mesh_selection_tests.c
 * @brief Tests for mesh selection mode system: edge table, selection
 *        conversion (face↔vertex, face↔edge), mode command.
 *
 * Uses a simple test mesh: a quad (2 triangles, 4 vertices, 5 edges).
 *
 *   v0---v1
 *   | \  |
 *   |  \ |
 *   v2---v3
 *
 *   Face 0: v0, v3, v2 (polygroup 0)
 *   Face 1: v0, v1, v3 (polygroup 0)
 *   Edges: (0,2), (0,3), (2,3), (0,1), (1,3)  = 5 unique edges
 */
#include <stdio.h>
#include <string.h>
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

/* ------------------------------------------------------------------ */
/* Helper: build the standard quad test mesh                           */
/* ------------------------------------------------------------------ */

static void build_quad_mesh_(mesh_slot_t *slot) {
    mesh_slot_init(slot, 8, 24);

    float p0[3] = {0, 1, 0}, p1[3] = {1, 1, 0};
    float p2[3] = {0, 0, 0}, p3[3] = {1, 0, 0};
    float n[3]  = {0, 0, 1};

    mesh_slot_add_vertex(slot, p0, n); /* v0 */
    mesh_slot_add_vertex(slot, p1, n); /* v1 */
    mesh_slot_add_vertex(slot, p2, n); /* v2 */
    mesh_slot_add_vertex(slot, p3, n); /* v3 */

    mesh_slot_add_triangle(slot, 0, 3, 2, 0); /* face 0 */
    mesh_slot_add_triangle(slot, 0, 1, 3, 0); /* face 1 */
}

/* ------------------------------------------------------------------ */
/* Edge table tests                                                    */
/* ------------------------------------------------------------------ */

static void test_edge_table_build(void) {
    mesh_slot_t slot;
    build_quad_mesh_(&slot);

    mesh_edge_table_t table;
    bool ok = mesh_edge_table_build(&table, &slot);
    ASSERT(ok, "edge table build should succeed");
    ASSERT(table.edge_count == 5, "quad has 5 unique edges");

    /* Verify all edges exist */
    ASSERT(mesh_edge_table_find(&table, 0, 2) != UINT32_MAX, "edge 0-2");
    ASSERT(mesh_edge_table_find(&table, 0, 3) != UINT32_MAX, "edge 0-3");
    ASSERT(mesh_edge_table_find(&table, 2, 3) != UINT32_MAX, "edge 2-3");
    ASSERT(mesh_edge_table_find(&table, 0, 1) != UINT32_MAX, "edge 0-1");
    ASSERT(mesh_edge_table_find(&table, 1, 3) != UINT32_MAX, "edge 1-3");

    /* Order doesn't matter */
    uint32_t e1 = mesh_edge_table_find(&table, 0, 3);
    uint32_t e2 = mesh_edge_table_find(&table, 3, 0);
    ASSERT(e1 == e2, "edge find is symmetric");

    /* Non-existent edge */
    ASSERT(mesh_edge_table_find(&table, 1, 2) == UINT32_MAX, "no edge 1-2");

    mesh_edge_table_destroy(&table);
    mesh_slot_destroy(&slot);
    g_pass++;
}

static void test_edge_table_empty_mesh(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 0, 0);

    mesh_edge_table_t table;
    bool ok = mesh_edge_table_build(&table, &slot);
    ASSERT(ok, "empty mesh edge table should succeed");
    ASSERT(table.edge_count == 0, "no edges");

    mesh_edge_table_destroy(&table);
    mesh_slot_destroy(&slot);
    g_pass++;
}

static void test_edge_table_null(void) {
    mesh_edge_table_t table;
    bool ok = mesh_edge_table_build(&table, NULL);
    ASSERT(!ok, "NULL slot should fail");
    ASSERT(mesh_edge_table_find(NULL, 0, 1) == UINT32_MAX, "find NULL");
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Selection conversion: face → vertex                                 */
/* ------------------------------------------------------------------ */

static void test_face_to_vertex(void) {
    mesh_slot_t slot;
    build_quad_mesh_(&slot);

    /* Select face 0 (vertices 0, 3, 2) */
    mesh_sel_bitset_t faces, verts;
    mesh_sel_bitset_init(&faces);
    mesh_sel_bitset_init(&verts);

    mesh_sel_bitset_set(&faces, 0);

    mesh_sel_convert_face_to_vertex(&slot, &faces, &verts);
    ASSERT(verts.count == 3, "3 vertices from face 0");
    ASSERT(mesh_sel_bitset_test(&verts, 0), "v0 selected");
    ASSERT(mesh_sel_bitset_test(&verts, 2), "v2 selected");
    ASSERT(mesh_sel_bitset_test(&verts, 3), "v3 selected");
    ASSERT(!mesh_sel_bitset_test(&verts, 1), "v1 not selected");

    mesh_sel_bitset_destroy(&faces);
    mesh_sel_bitset_destroy(&verts);
    mesh_slot_destroy(&slot);
    g_pass++;
}

static void test_face_to_vertex_both_faces(void) {
    mesh_slot_t slot;
    build_quad_mesh_(&slot);

    mesh_sel_bitset_t faces, verts;
    mesh_sel_bitset_init(&faces);
    mesh_sel_bitset_init(&verts);

    mesh_sel_bitset_set(&faces, 0);
    mesh_sel_bitset_set(&faces, 1);

    mesh_sel_convert_face_to_vertex(&slot, &faces, &verts);
    ASSERT(verts.count == 4, "all 4 vertices from both faces");

    mesh_sel_bitset_destroy(&faces);
    mesh_sel_bitset_destroy(&verts);
    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Selection conversion: vertex → face                                 */
/* ------------------------------------------------------------------ */

static void test_vertex_to_face(void) {
    mesh_slot_t slot;
    build_quad_mesh_(&slot);

    /* Select vertices 0, 3, 2 — should select face 0 (all 3 verts) */
    mesh_sel_bitset_t verts, faces;
    mesh_sel_bitset_init(&verts);
    mesh_sel_bitset_init(&faces);

    mesh_sel_bitset_set(&verts, 0);
    mesh_sel_bitset_set(&verts, 3);
    mesh_sel_bitset_set(&verts, 2);

    mesh_sel_convert_vertex_to_face(&slot, &verts, &faces);
    ASSERT(mesh_sel_bitset_test(&faces, 0), "face 0 selected (all verts present)");
    /* Face 1 (0,1,3) — vertex 1 not selected, so face 1 should NOT be selected */
    ASSERT(!mesh_sel_bitset_test(&faces, 1), "face 1 not selected (v1 missing)");

    mesh_sel_bitset_destroy(&verts);
    mesh_sel_bitset_destroy(&faces);
    mesh_slot_destroy(&slot);
    g_pass++;
}

static void test_vertex_to_face_all_selected(void) {
    mesh_slot_t slot;
    build_quad_mesh_(&slot);

    mesh_sel_bitset_t verts, faces;
    mesh_sel_bitset_init(&verts);
    mesh_sel_bitset_init(&faces);

    /* Select all 4 vertices — both faces selected */
    for (uint32_t i = 0; i < 4; i++) { mesh_sel_bitset_set(&verts, i); }

    mesh_sel_convert_vertex_to_face(&slot, &verts, &faces);
    ASSERT(faces.count == 2, "both faces selected");

    mesh_sel_bitset_destroy(&verts);
    mesh_sel_bitset_destroy(&faces);
    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Selection conversion: face → edge                                   */
/* ------------------------------------------------------------------ */

static void test_face_to_edge(void) {
    mesh_slot_t slot;
    build_quad_mesh_(&slot);

    mesh_edge_table_t table;
    mesh_edge_table_build(&table, &slot);

    mesh_sel_bitset_t faces, edges;
    mesh_sel_bitset_init(&faces);
    mesh_sel_bitset_init(&edges);

    /* Select face 0 (verts 0,3,2) → edges (0,3), (3,2), (0,2) */
    mesh_sel_bitset_set(&faces, 0);

    mesh_sel_convert_face_to_edge(&slot, &table, &faces, &edges);
    ASSERT(edges.count == 3, "3 edges from face 0");

    uint32_t e02 = mesh_edge_table_find(&table, 0, 2);
    uint32_t e03 = mesh_edge_table_find(&table, 0, 3);
    uint32_t e23 = mesh_edge_table_find(&table, 2, 3);

    ASSERT(mesh_sel_bitset_test(&edges, e02), "edge 0-2 selected");
    ASSERT(mesh_sel_bitset_test(&edges, e03), "edge 0-3 selected");
    ASSERT(mesh_sel_bitset_test(&edges, e23), "edge 2-3 selected");

    mesh_sel_bitset_destroy(&faces);
    mesh_sel_bitset_destroy(&edges);
    mesh_edge_table_destroy(&table);
    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Selection conversion: edge → face                                   */
/* ------------------------------------------------------------------ */

static void test_edge_to_face(void) {
    mesh_slot_t slot;
    build_quad_mesh_(&slot);

    mesh_edge_table_t table;
    mesh_edge_table_build(&table, &slot);

    mesh_sel_bitset_t edges, faces;
    mesh_sel_bitset_init(&edges);
    mesh_sel_bitset_init(&faces);

    /* Select edge 0-3 — shared between face 0 and face 1 */
    uint32_t e03 = mesh_edge_table_find(&table, 0, 3);
    mesh_sel_bitset_set(&edges, e03);

    mesh_sel_convert_edge_to_face(&slot, &table, &edges, &faces);
    /* Both faces contain edge 0-3 */
    ASSERT(mesh_sel_bitset_test(&faces, 0), "face 0 contains edge 0-3");
    ASSERT(mesh_sel_bitset_test(&faces, 1), "face 1 contains edge 0-3");

    mesh_sel_bitset_destroy(&edges);
    mesh_sel_bitset_destroy(&faces);
    mesh_edge_table_destroy(&table);
    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Empty selection conversions                                         */
/* ------------------------------------------------------------------ */

static void test_empty_conversion(void) {
    mesh_slot_t slot;
    build_quad_mesh_(&slot);

    mesh_sel_bitset_t src, dst;
    mesh_sel_bitset_init(&src);
    mesh_sel_bitset_init(&dst);

    mesh_sel_convert_face_to_vertex(&slot, &src, &dst);
    ASSERT(dst.count == 0, "empty face→vertex yields empty");

    mesh_sel_convert_vertex_to_face(&slot, &src, &dst);
    ASSERT(dst.count == 0, "empty vertex→face yields empty");

    mesh_sel_bitset_destroy(&src);
    mesh_sel_bitset_destroy(&dst);
    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Single triangle mesh (boundary case)                                */
/* ------------------------------------------------------------------ */

static void test_single_triangle(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 4, 12);

    float p0[3] = {0,0,0}, p1[3] = {1,0,0}, p2[3] = {0,1,0};
    float n[3]  = {0,0,1};
    mesh_slot_add_vertex(&slot, p0, n);
    mesh_slot_add_vertex(&slot, p1, n);
    mesh_slot_add_vertex(&slot, p2, n);
    mesh_slot_add_triangle(&slot, 0, 1, 2, 0);

    mesh_edge_table_t table;
    mesh_edge_table_build(&table, &slot);
    ASSERT(table.edge_count == 3, "single tri has 3 edges");

    mesh_edge_table_destroy(&table);
    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ================================================================== */
/* Main                                                                */
/* ================================================================== */

int main(void) {
    printf("mesh_selection_tests:\n");

    test_edge_table_build();
    test_edge_table_empty_mesh();
    test_edge_table_null();
    test_face_to_vertex();
    test_face_to_vertex_both_faces();
    test_vertex_to_face();
    test_vertex_to_face_all_selected();
    test_face_to_edge();
    test_edge_to_face();
    test_empty_conversion();
    test_single_triangle();

    printf("  %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
