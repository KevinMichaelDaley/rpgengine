/**
 * @file mesh_primitives_tests.c
 * @brief Tests for mesh primitive generators: box, cylinder, sphere, plane.
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "ferrum/editor/mesh/mesh_primitives.h"

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
/* Helper: check all indices in range                                  */
/* ------------------------------------------------------------------ */

static bool indices_valid_(const mesh_slot_t *slot) {
    for (uint32_t i = 0; i < slot->index_count; i++) {
        if (slot->indices[i] >= slot->vertex_count) { return false; }
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Helper: check normals are unit-length                               */
/* ------------------------------------------------------------------ */

static bool normals_unit_(const mesh_slot_t *slot) {
    for (uint32_t i = 0; i < slot->vertex_count; i++) {
        float nx = slot->normals[i * 3 + 0];
        float ny = slot->normals[i * 3 + 1];
        float nz = slot->normals[i * 3 + 2];
        float len = sqrtf(nx*nx + ny*ny + nz*nz);
        if (fabsf(len - 1.0f) > 0.01f) { return false; }
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Helper: check UVs in [0,1]                                          */
/* ------------------------------------------------------------------ */

static bool uvs_in_range_(const mesh_slot_t *slot) {
    for (uint32_t i = 0; i < slot->vertex_count * 2; i++) {
        float u = slot->uvs[0][i];
        if (u < -0.01f || u > 1.01f) { return false; }
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Box tests                                                           */
/* ------------------------------------------------------------------ */

static void test_box_unit(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 32, 48);

    float size[3] = {1.0f, 1.0f, 1.0f};
    float pos[3]  = {0.0f, 0.0f, 0.0f};
    uint32_t segs[3] = {1, 1, 1};

    bool ok = mesh_prim_box(&slot, size, segs, pos);
    ASSERT(ok, "box gen succeeded");

    /* 6 faces × 4 verts = 24 vertices (unshared for correct normals) */
    ASSERT(slot.vertex_count == 24, "24 verts for unit box");
    /* 6 faces × 2 tris × 3 = 36 indices */
    ASSERT(slot.index_count == 36, "36 indices for unit box");
    ASSERT(indices_valid_(&slot), "all indices in range");
    ASSERT(normals_unit_(&slot), "normals are unit length");
    ASSERT(uvs_in_range_(&slot), "UVs in [0,1]");

    mesh_slot_destroy(&slot);
    g_pass++;
}

static void test_box_offset(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 32, 48);

    float size[3] = {2.0f, 4.0f, 2.0f};
    float pos[3]  = {10.0f, 20.0f, 30.0f};
    uint32_t segs[3] = {1, 1, 1};

    mesh_prim_box(&slot, size, segs, pos);

    /* Check that vertices are offset by pos */
    bool found_offset = false;
    for (uint32_t i = 0; i < slot.vertex_count; i++) {
        float x = slot.positions[i * 3 + 0];
        if (fabsf(x - 9.0f) < 0.01f || fabsf(x - 11.0f) < 0.01f) {
            found_offset = true;
            break;
        }
    }
    ASSERT(found_offset, "vertices offset by position");

    mesh_slot_destroy(&slot);
    g_pass++;
}

static void test_box_segmented(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 256, 1024);

    float size[3] = {4.0f, 4.0f, 4.0f};
    float pos[3]  = {0, 0, 0};
    uint32_t segs[3] = {2, 2, 2};

    mesh_prim_box(&slot, size, segs, pos);

    /* With 2 segments per side, each face has 4 quads = 8 tris.
     * 6 faces × (2+1)*(2+1) verts = 6 × 9 = 54 vertices
     * 6 faces × 2*2 quads × 2 tris × 3 indices = 6*4*6 = 144 indices */
    ASSERT(slot.vertex_count == 54, "54 verts for 2-seg box");
    ASSERT(slot.index_count == 144, "144 indices for 2-seg box");
    ASSERT(indices_valid_(&slot), "indices valid");

    mesh_slot_destroy(&slot);
    g_pass++;
}

static void test_box_null(void) {
    float size[3] = {1,1,1};
    float pos[3] = {0,0,0};
    uint32_t segs[3] = {1,1,1};
    ASSERT(!mesh_prim_box(NULL, size, segs, pos), "null slot fails");
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Plane tests                                                         */
/* ------------------------------------------------------------------ */

static void test_plane_basic(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 32, 48);

    float size[2] = {4.0f, 4.0f};
    float pos[3]  = {0, 0, 0};
    uint32_t segs[2] = {1, 1};

    bool ok = mesh_prim_plane(&slot, size, segs, 1 /* Y-up */, pos);
    ASSERT(ok, "plane gen succeeded");

    /* 1 segment = 2×2 = 4 vertices, 2 tris = 6 indices */
    ASSERT(slot.vertex_count == 4, "4 verts");
    ASSERT(slot.index_count == 6, "6 indices");
    ASSERT(indices_valid_(&slot), "indices valid");
    ASSERT(normals_unit_(&slot), "normals unit");
    ASSERT(uvs_in_range_(&slot), "UVs in range");

    mesh_slot_destroy(&slot);
    g_pass++;
}

static void test_plane_subdivided(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 256, 1024);

    float size[2] = {10.0f, 10.0f};
    float pos[3]  = {0, 0, 0};
    uint32_t segs[2] = {4, 4};

    mesh_prim_plane(&slot, size, segs, 1, pos);

    /* 4 segs = 5×5 = 25 verts, 4×4×2 = 32 tris = 96 indices */
    ASSERT(slot.vertex_count == 25, "25 verts");
    ASSERT(slot.index_count == 96, "96 indices");

    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Cylinder tests                                                      */
/* ------------------------------------------------------------------ */

static void test_cylinder_basic(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 256, 1024);

    bool ok = mesh_prim_cylinder(&slot, 1.0f, 2.0f, 8, 1 /* Y axis */, (float[3]){0,0,0});
    ASSERT(ok, "cylinder gen succeeded");

    /* 8 segments: side = 2 rings × 8 = 16 verts, top/bottom caps each 8+1 = 9
     * Total = 16 + 9 + 9 = 34 verts
     * Side tris: 8 quads × 2 = 16, caps: 8 tris each = 16, total = 32 tris = 96 indices */
    ASSERT(slot.vertex_count > 0, "has vertices");
    ASSERT(slot.index_count > 0, "has indices");
    ASSERT(indices_valid_(&slot), "indices valid");
    ASSERT(normals_unit_(&slot), "normals unit");

    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ------------------------------------------------------------------ */
/* Sphere tests                                                        */
/* ------------------------------------------------------------------ */

static void test_sphere_basic(void) {
    mesh_slot_t slot;
    mesh_slot_init(&slot, 512, 2048);

    bool ok = mesh_prim_sphere(&slot, 1.0f, 8, 0, (float[3]){0,0,0});
    ASSERT(ok, "sphere gen succeeded");
    ASSERT(slot.vertex_count > 0, "has vertices");
    ASSERT(slot.index_count > 0, "has indices");
    ASSERT(indices_valid_(&slot), "indices valid");
    ASSERT(normals_unit_(&slot), "normals unit");

    /* Check that positions are on the unit sphere (radius 1) */
    for (uint32_t i = 0; i < slot.vertex_count; i++) {
        float x = slot.positions[i*3+0];
        float y = slot.positions[i*3+1];
        float z = slot.positions[i*3+2];
        float r = sqrtf(x*x + y*y + z*z);
        ASSERT(fabsf(r - 1.0f) < 0.01f, "vertex on sphere");
    }

    mesh_slot_destroy(&slot);
    g_pass++;
}

/* ================================================================== */
/* Main                                                                */
/* ================================================================== */

int main(void) {
    printf("mesh_primitives_tests:\n");

    test_box_unit();
    test_box_offset();
    test_box_segmented();
    test_box_null();
    test_plane_basic();
    test_plane_subdivided();
    test_cylinder_basic();
    test_sphere_basic();

    printf("  %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
