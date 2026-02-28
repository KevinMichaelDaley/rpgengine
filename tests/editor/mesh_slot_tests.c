/**
 * @file mesh_slot_tests.c
 * @brief Unit tests for mesh_slot_t — editable mesh data structure.
 *
 * Tests cover: init, destroy, reserve capacity, clear, add vertices,
 * add indices, edge cases (null, overflow, zero).
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "ferrum/editor/mesh/mesh_slot.h"

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
/* Init / Destroy                                                      */
/* ------------------------------------------------------------------ */

static void test_init_default(void) {
    mesh_slot_t slot;
    bool ok = mesh_slot_init(&slot, 64, 192);
    ASSERT(ok, "init should succeed");
    ASSERT(slot.vertex_count == 0, "should start empty");
    ASSERT(slot.index_count == 0, "should start with no indices");
    ASSERT(slot.vertex_capacity >= 64, "should have requested vert capacity");
    ASSERT(slot.index_capacity >= 192, "should have requested idx capacity");
    ASSERT(slot.positions != NULL, "positions buffer allocated");
    ASSERT(slot.normals != NULL, "normals buffer allocated");
    ASSERT(slot.uvs[0] != NULL, "uv0 buffer allocated");
    ASSERT(slot.uvs[1] != NULL, "uv1 buffer allocated");
    ASSERT(slot.colors != NULL, "colors buffer allocated");
    ASSERT(slot.tangents != NULL, "tangents buffer allocated");
    ASSERT(slot.indices != NULL, "indices buffer allocated");
    ASSERT(slot.polygroup_ids != NULL, "polygroup buffer allocated");
    mesh_slot_destroy(&slot);
    g_pass++;
}

static void test_init_null(void) {
    bool ok = mesh_slot_init(NULL, 64, 192);
    ASSERT(!ok, "init with NULL should fail");
    g_pass++;
}

static void test_init_zero_capacity(void) {
    mesh_slot_t slot;
    /* Zero capacity should still succeed — just means no pre-alloc */
    bool ok = mesh_slot_init(&slot, 0, 0);
    ASSERT(ok, "init with zero should succeed");
    ASSERT(slot.vertex_count == 0, "zero init has zero vertices");
    ASSERT(slot.index_count == 0, "zero init has zero indices");
    mesh_slot_destroy(&slot);
    g_pass++;
}

static void test_destroy_null(void) {
    /* Should not crash */
    mesh_slot_destroy(NULL);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Reserve                                                             */
/* ------------------------------------------------------------------ */

static void test_reserve_vertices(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 16, 48);
    bool ok = mesh_slot_reserve_vertices(&slot, 256);
    ASSERT(ok, "reserve should succeed");
    ASSERT(slot.vertex_capacity >= 256, "capacity should grow");
    ASSERT(slot.vertex_count == 0, "count unchanged after reserve");
    mesh_slot_destroy(&slot);
    g_pass++;
}

static void test_reserve_indices(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 16, 48);
    bool ok = mesh_slot_reserve_indices(&slot, 1024);
    ASSERT(ok, "reserve indices should succeed");
    ASSERT(slot.index_capacity >= 1024, "index capacity should grow");
    ASSERT(slot.index_count == 0, "count unchanged after reserve");
    mesh_slot_destroy(&slot);
    g_pass++;
}

static void test_reserve_no_shrink(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 256, 768);
    uint32_t cap_before = slot.vertex_capacity;
    bool ok = mesh_slot_reserve_vertices(&slot, 16);
    ASSERT(ok, "smaller reserve should succeed (no-op)");
    ASSERT(slot.vertex_capacity >= cap_before, "capacity should not shrink");
    mesh_slot_destroy(&slot);
    g_pass++;
}

static void test_reserve_null(void) {
    bool ok = mesh_slot_reserve_vertices(NULL, 256);
    ASSERT(!ok, "reserve null should fail");
    ok = mesh_slot_reserve_indices(NULL, 256);
    ASSERT(!ok, "reserve indices null should fail");
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Clear                                                               */
/* ------------------------------------------------------------------ */

static void test_clear(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 64, 192);

    /* Manually set counts to simulate data */
    slot.vertex_count = 32;
    slot.index_count = 96;

    mesh_slot_clear(&slot);
    ASSERT(slot.vertex_count == 0, "vertices cleared");
    ASSERT(slot.index_count == 0, "indices cleared");
    /* Capacity should remain — clear doesn't free */
    ASSERT(slot.vertex_capacity >= 64, "capacity retained after clear");
    mesh_slot_destroy(&slot);
    g_pass++;
}

static void test_clear_null(void) {
    /* Should not crash */
    mesh_slot_clear(NULL);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Add vertices                                                        */
/* ------------------------------------------------------------------ */

static void test_add_vertex(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 4, 12);

    float pos[3] = {1.0f, 2.0f, 3.0f};
    float nrm[3] = {0.0f, 1.0f, 0.0f};
    uint32_t idx = mesh_slot_add_vertex(&slot, pos, nrm);
    ASSERT(idx == 0, "first vertex index is 0");
    ASSERT(slot.vertex_count == 1, "count incremented");
    ASSERT(fabsf(slot.positions[0] - 1.0f) < 1e-6f, "pos.x stored");
    ASSERT(fabsf(slot.positions[1] - 2.0f) < 1e-6f, "pos.y stored");
    ASSERT(fabsf(slot.positions[2] - 3.0f) < 1e-6f, "pos.z stored");
    ASSERT(fabsf(slot.normals[0] - 0.0f) < 1e-6f, "nrm.x stored");
    ASSERT(fabsf(slot.normals[1] - 1.0f) < 1e-6f, "nrm.y stored");

    float pos2[3] = {4.0f, 5.0f, 6.0f};
    uint32_t idx2 = mesh_slot_add_vertex(&slot, pos2, nrm);
    ASSERT(idx2 == 1, "second vertex index is 1");
    ASSERT(slot.vertex_count == 2, "count is 2");

    mesh_slot_destroy(&slot);
    g_pass++;
}

static void test_add_vertex_auto_grow(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 2, 6);

    float pos[3] = {0};
    float nrm[3] = {0, 1, 0};
    /* Add more than initial capacity */
    for (int i = 0; i < 10; i++) {
        pos[0] = (float)i;
        uint32_t idx = mesh_slot_add_vertex(&slot, pos, nrm);
        ASSERT(idx == (uint32_t)i, "index matches count");
    }
    ASSERT(slot.vertex_count == 10, "all 10 vertices added");
    ASSERT(slot.vertex_capacity >= 10, "capacity grew");
    ASSERT(fabsf(slot.positions[9 * 3] - 9.0f) < 1e-6f, "last pos correct");

    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Add triangle                                                        */
/* ------------------------------------------------------------------ */

static void test_add_triangle(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 4, 12);

    bool ok = mesh_slot_add_triangle(&slot, 0, 1, 2, 0);
    ASSERT(ok, "add triangle should succeed");
    ASSERT(slot.index_count == 3, "3 indices added");
    ASSERT(slot.indices[0] == 0, "i0");
    ASSERT(slot.indices[1] == 1, "i1");
    ASSERT(slot.indices[2] == 2, "i2");
    ASSERT(slot.polygroup_ids[0] == 0, "polygroup set");

    mesh_slot_destroy(&slot);
    g_pass++;
}

static void test_add_triangle_auto_grow(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 16, 3); /* only room for 1 triangle */

    for (uint32_t i = 0; i < 10; i++) {
        bool ok = mesh_slot_add_triangle(&slot, i*3, i*3+1, i*3+2, (uint16_t)i);
        ASSERT(ok, "triangle add should succeed");
    }
    ASSERT(slot.index_count == 30, "30 indices (10 tris)");
    ASSERT(slot.polygroup_ids[9] == 9, "last polygroup correct");

    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Face count helper                                                   */
/* ------------------------------------------------------------------ */

static void test_face_count(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 16, 48);
    ASSERT(mesh_slot_face_count(&slot) == 0, "0 faces initially");

    mesh_slot_add_triangle(&slot, 0, 1, 2, 0);
    mesh_slot_add_triangle(&slot, 3, 4, 5, 0);
    ASSERT(mesh_slot_face_count(&slot) == 2, "2 faces after 2 tris");

    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Capacity limits                                                     */
/* ------------------------------------------------------------------ */

static void test_max_vertex_limit(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 4, 12);

    /* Try to reserve beyond max — should fail gracefully */
    bool ok = mesh_slot_reserve_vertices(&slot, MESH_SLOT_MAX_VERTICES + 1);
    ASSERT(!ok, "reserve beyond max should fail");

    mesh_slot_destroy(&slot);
    g_pass++;
}

static void test_max_index_limit(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 4, 12);

    bool ok = mesh_slot_reserve_indices(&slot, MESH_SLOT_MAX_INDICES + 1);
    ASSERT(!ok, "reserve beyond max should fail");

    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ================================================================== */
/* Main                                                                */
/* ================================================================== */

int main(void) {
    printf("mesh_slot_tests:\n");

    test_init_default();
    test_init_null();
    test_init_zero_capacity();
    test_destroy_null();
    test_reserve_vertices();
    test_reserve_indices();
    test_reserve_no_shrink();
    test_reserve_null();
    test_clear();
    test_clear_null();
    test_add_vertex();
    test_add_vertex_auto_grow();
    test_add_triangle();
    test_add_triangle_auto_grow();
    test_face_count();
    test_max_vertex_limit();
    test_max_index_limit();

    printf("  %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
