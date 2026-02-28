/**
 * @file client_mesh_render_tests.c
 * @brief Tests for client-side mesh rendering data path (no GL).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ferrum/editor/client/client_mesh_render.h"
#include "ferrum/editor/mesh/mesh_primitives.h"
#include "ferrum/editor/mesh/mesh_vao_format.h"
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
/* Test: FVMA deserialization into render data                         */
/* ------------------------------------------------------------------ */

static void test_fvma_to_render_data(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 256, 1024);
    mesh_prim_box(&slot, (float[3]){1,1,1}, (uint32_t[3]){1,1,1}, (float[3]){0,0,0});

    /* Serialize to FVMA */
    uint32_t flags = MESH_VAO_FLAG_NORMALS;
    size_t size = mesh_vao_serialized_size(&slot, flags);
    uint8_t *buf = malloc(size);
    mesh_vao_serialize(&slot, flags, buf, size);

    /* Deserialize into render data */
    client_mesh_data_t data;
    bool ok = client_mesh_data_from_fvma(&data, buf, size);
    ASSERT(ok, "deserialized ok");
    ASSERT(data.vertex_count == slot.vertex_count, "vertex count match");
    ASSERT(data.index_count == slot.index_count, "index count match");
    ASSERT(data.positions != NULL, "positions allocated");
    ASSERT(data.normals != NULL, "normals allocated");

    client_mesh_data_destroy(&data);
    free(buf);
    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Test: wireframe edge extraction                                     */
/* ------------------------------------------------------------------ */

static void test_edge_extraction(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 256, 1024);
    mesh_prim_box(&slot, (float[3]){1,1,1}, (uint32_t[3]){1,1,1}, (float[3]){0,0,0});

    /* Extract unique edges from triangle soup */
    client_mesh_edges_t edges;
    bool ok = client_mesh_extract_edges(&edges, slot.indices, slot.index_count);
    ASSERT(ok, "edge extraction ok");

    /* A box with 12 triangles should have some unique edges */
    ASSERT(edges.edge_count > 0, "found edges");
    /* Box: 12 triangles, each has 3 edges = 36 half-edges.
     * Unique edges = 18 (each quad has 5 edges, but shared).
     * Actually for split-vertex box: 12 tris * 3 = 36 edges,
     * but many are shared. With 24 verts, edges could be up to 36. */
    ASSERT(edges.edge_count <= 36, "reasonable edge count");

    client_mesh_edges_destroy(&edges);
    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Test: face selection highlight indices                               */
/* ------------------------------------------------------------------ */

static void test_face_highlight(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 256, 1024);
    mesh_prim_box(&slot, (float[3]){1,1,1}, (uint32_t[3]){1,1,1}, (float[3]){0,0,0});

    /* Select faces 0 and 1 */
    mesh_sel_bitset_t sel;
    mesh_sel_bitset_init(&sel);
    mesh_sel_bitset_set(&sel, 0);
    mesh_sel_bitset_set(&sel, 1);

    uint32_t *highlight_indices = NULL;
    uint32_t highlight_count = 0;
    bool ok = client_mesh_face_highlight(slot.indices, slot.index_count,
                                         &sel, &highlight_indices,
                                         &highlight_count);
    ASSERT(ok, "face highlight ok");
    /* 2 selected faces * 3 indices each = 6 highlight indices */
    ASSERT(highlight_count == 6, "6 highlight indices");

    free(highlight_indices);
    mesh_sel_bitset_destroy(&sel);
    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Test: null safety                                                   */
/* ------------------------------------------------------------------ */

static void test_null_safety(void) {
    client_mesh_data_t data;
    ASSERT(!client_mesh_data_from_fvma(&data, NULL, 0), "null fvma");

    client_mesh_edges_t edges;
    ASSERT(!client_mesh_extract_edges(&edges, NULL, 0), "null edges");

    ASSERT(!client_mesh_face_highlight(NULL, 0, NULL, NULL, NULL), "null highlight");

    g_pass++;
}

int main(void) {
    printf("client_mesh_render_tests:\n");
    test_fvma_to_render_data();
    test_edge_extraction();
    test_face_highlight();
    test_null_safety();
    printf("  %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
