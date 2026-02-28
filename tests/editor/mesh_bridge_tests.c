/**
 * @file mesh_bridge_tests.c
 * @brief Tests for bridge edges and connect vertices.
 */
#include <stdio.h>
#include "ferrum/editor/mesh/mesh_bridge.h"
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

static void test_bridge_two_edges(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 16, 96);

    float n[3] = {0, 0, 1};
    /* Loop A: square at z=0 */
    mesh_slot_add_vertex(&slot, (float[3]){0,0,0}, n);
    mesh_slot_add_vertex(&slot, (float[3]){1,0,0}, n);
    mesh_slot_add_vertex(&slot, (float[3]){1,1,0}, n);
    mesh_slot_add_vertex(&slot, (float[3]){0,1,0}, n);
    /* Loop B: square at z=1 */
    mesh_slot_add_vertex(&slot, (float[3]){0,0,1}, n);
    mesh_slot_add_vertex(&slot, (float[3]){1,0,1}, n);
    mesh_slot_add_vertex(&slot, (float[3]){1,1,1}, n);
    mesh_slot_add_vertex(&slot, (float[3]){0,1,1}, n);

    uint32_t loop_a[4] = {0, 1, 2, 3};
    uint32_t loop_b[4] = {4, 5, 6, 7};

    bool ok = mesh_bridge_edges(&slot, loop_a, loop_b, 4);
    ASSERT(ok, "bridge succeeded");
    /* 4 quads = 8 triangles */
    ASSERT(slot.index_count / 3 == 8, "8 faces");
    ASSERT(indices_valid_(&slot), "indices valid");

    mesh_slot_destroy(&slot);
    g_pass++;
}

static void test_bridge_two_verts(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 8, 24);

    float n[3] = {0, 0, 1};
    mesh_slot_add_vertex(&slot, (float[3]){0,0,0}, n);
    mesh_slot_add_vertex(&slot, (float[3]){1,0,0}, n);

    uint32_t a[2] = {0, 1};
    uint32_t b[2] = {0, 1};

    /* Bridge with 2 vertices — creates 2 degenerate tris (edge loop of 2) */
    bool ok = mesh_bridge_edges(&slot, a, b, 2);
    ASSERT(ok, "bridge 2-vert succeeded");

    mesh_slot_destroy(&slot);
    g_pass++;
}

static void test_connect_vertices(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 8, 24);

    float n[3] = {0, 0, 1};
    mesh_slot_add_vertex(&slot, (float[3]){0,0,0}, n);
    mesh_slot_add_vertex(&slot, (float[3]){1,0,0}, n);
    mesh_slot_add_vertex(&slot, (float[3]){0,1,0}, n);
    mesh_slot_add_triangle(&slot, 0, 1, 2, 0);

    /* In a triangle, all vertices are adjacent — connect is a no-op */
    bool ok = mesh_connect_vertices(&slot, 0, 2);
    ASSERT(ok, "connect in triangle");

    mesh_slot_destroy(&slot);
    g_pass++;
}

static void test_null_safety(void) {
    ASSERT(!mesh_bridge_edges(NULL, NULL, NULL, 0), "null bridge");
    ASSERT(!mesh_connect_vertices(NULL, 0, 0), "null connect");
    g_pass++;
}

int main(void) {
    printf("mesh_bridge_tests:\n");
    test_bridge_two_edges();
    test_bridge_two_verts();
    test_connect_vertices();
    test_null_safety();
    printf("  %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
