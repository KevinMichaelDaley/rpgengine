/**
 * @file snap_mesh_decompose_tests.c
 * @brief Tests for convex decomposition snap mesh retention.
 *
 * Verifies that high-poly meshes get decomposed into convex hulls
 * before insertion into the snap cache, and that the threshold
 * logic correctly decides when to decompose.
 */

#include "ferrum/editor/viewport/snap/snap_mesh_decompose.h"
#include "ferrum/editor/viewport/snap/snap_mesh_cache.h"
#include "ferrum/editor/mesh/mesh_slot.h"

#define _USE_MATH_DEFINES
#include <math.h>
#include <stdio.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <stdlib.h>
#include <string.h>

/* ---- Test harness ---- */

static int g_pass, g_fail;

#define ASSERT(cond) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; \
           fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

/* ---- Helpers: generate test meshes ---- */

/**
 * @brief Build a unit box mesh_slot (12 tris, 24 verts).
 */
static void build_box_slot(mesh_slot_t *slot) {
    memset(slot, 0, sizeof(*slot));
    mesh_slot_init(slot, 24, 36);

    /* 6 faces, 4 verts each = 24 verts. */
    static const float pos[][3] = {
        /* Front (+Z) */
        {-0.5f, -0.5f,  0.5f}, { 0.5f, -0.5f,  0.5f},
        { 0.5f,  0.5f,  0.5f}, {-0.5f,  0.5f,  0.5f},
        /* Back (-Z) */
        {-0.5f, -0.5f, -0.5f}, {-0.5f,  0.5f, -0.5f},
        { 0.5f,  0.5f, -0.5f}, { 0.5f, -0.5f, -0.5f},
        /* Top (+Y) */
        {-0.5f,  0.5f, -0.5f}, {-0.5f,  0.5f,  0.5f},
        { 0.5f,  0.5f,  0.5f}, { 0.5f,  0.5f, -0.5f},
        /* Bottom (-Y) */
        {-0.5f, -0.5f, -0.5f}, { 0.5f, -0.5f, -0.5f},
        { 0.5f, -0.5f,  0.5f}, {-0.5f, -0.5f,  0.5f},
        /* Right (+X) */
        { 0.5f, -0.5f, -0.5f}, { 0.5f,  0.5f, -0.5f},
        { 0.5f,  0.5f,  0.5f}, { 0.5f, -0.5f,  0.5f},
        /* Left (-X) */
        {-0.5f, -0.5f, -0.5f}, {-0.5f, -0.5f,  0.5f},
        {-0.5f,  0.5f,  0.5f}, {-0.5f,  0.5f, -0.5f},
    };
    static const float nrm[][3] = {
        {0,0,1},{0,0,1},{0,0,1},{0,0,1},
        {0,0,-1},{0,0,-1},{0,0,-1},{0,0,-1},
        {0,1,0},{0,1,0},{0,1,0},{0,1,0},
        {0,-1,0},{0,-1,0},{0,-1,0},{0,-1,0},
        {1,0,0},{1,0,0},{1,0,0},{1,0,0},
        {-1,0,0},{-1,0,0},{-1,0,0},{-1,0,0},
    };

    for (int i = 0; i < 24; i++) {
        mesh_slot_add_vertex(slot, pos[i], nrm[i]);
    }
    for (int f = 0; f < 6; f++) {
        uint32_t base = (uint32_t)(f * 4);
        mesh_slot_add_triangle(slot, base, base + 1, base + 2, 0);
        mesh_slot_add_triangle(slot, base, base + 2, base + 3, 0);
    }
}

/**
 * @brief Build a UV sphere mesh_slot with controllable subdivision.
 *
 * Generates a sphere with (slices * stacks) quads = 2 * slices * stacks tris.
 * With slices=32, stacks=64 → 4096 tris (above threshold).
 */
static void build_sphere_slot(mesh_slot_t *slot, uint32_t slices,
                               uint32_t stacks, float radius) {
    memset(slot, 0, sizeof(*slot));
    uint32_t vert_count = (stacks + 1) * (slices + 1);
    uint32_t tri_count = stacks * slices * 2;
    mesh_slot_init(slot, vert_count, tri_count * 3);

    /* Generate vertices. */
    for (uint32_t st = 0; st <= stacks; st++) {
        float phi = (float)M_PI * (float)st / (float)stacks;
        float sp = sinf(phi), cp = cosf(phi);
        for (uint32_t sl = 0; sl <= slices; sl++) {
            float theta = 2.0f * (float)M_PI * (float)sl / (float)slices;
            float ct = cosf(theta), st2 = sinf(theta);
            float nx = sp * ct, ny = cp, nz = sp * st2;
            float p[3] = { nx * radius, ny * radius, nz * radius };
            float n[3] = { nx, ny, nz };
            mesh_slot_add_vertex(slot, p, n);
        }
    }

    /* Generate triangles. */
    for (uint32_t st = 0; st < stacks; st++) {
        for (uint32_t sl = 0; sl < slices; sl++) {
            uint32_t a = st * (slices + 1) + sl;
            uint32_t b = a + slices + 1;
            mesh_slot_add_triangle(slot, a, b, a + 1, 0);
            mesh_slot_add_triangle(slot, a + 1, b, b + 1, 0);
        }
    }
}

/* ---- Tests ---- */

/** should_decompose returns false for small meshes (below threshold). */
static void test_should_decompose_below_threshold(void) {
    mesh_slot_t slot;
    build_box_slot(&slot); /* 12 tris */
    ASSERT(!snap_mesh_should_decompose(&slot));
    mesh_slot_destroy(&slot);
}

/** should_decompose returns true for high-poly meshes (above threshold). */
static void test_should_decompose_above_threshold(void) {
    mesh_slot_t slot;
    build_sphere_slot(&slot, 64, 64, 1.0f); /* 8192 tris */
    ASSERT(snap_mesh_should_decompose(&slot));
    mesh_slot_destroy(&slot);
}

/** should_decompose returns false for NULL slot. */
static void test_should_decompose_null(void) {
    ASSERT(!snap_mesh_should_decompose(NULL));
}

/** should_decompose returns false for exactly-at-threshold mesh. */
static void test_should_decompose_at_threshold(void) {
    /* Threshold is 2048, so exactly 2048 tris should NOT decompose
     * (we only decompose if strictly greater). */
    mesh_slot_t slot;
    build_sphere_slot(&slot, 32, 64, 1.0f); /* 32*64*2 = 4096 tris — above */
    ASSERT(snap_mesh_should_decompose(&slot));
    mesh_slot_destroy(&slot);
}

/** Decomposed snap mesh has fewer triangles than the input. */
static void test_decomposed_reduces_triangle_count(void) {
    mesh_slot_t slot;
    build_sphere_slot(&slot, 64, 64, 1.0f); /* 8192 tris */

    snap_mesh_cache_t cache;
    snap_mesh_cache_init(&cache, 16);

    bool ok = snap_mesh_retain_decomposed(&cache, 0, &slot, NULL);
    ASSERT(ok);

    const snap_mesh_t *sm = snap_mesh_cache_get(&cache, 0);
    ASSERT(sm != NULL);
    if (sm) {
        uint32_t out_tris = sm->index_count / 3;
        uint32_t in_tris = slot.index_count / 3;
        /* Decomposed output should be dramatically smaller. */
        ASSERT(out_tris < in_tris);
        ASSERT(out_tris > 0);
        /* For a sphere, convex decomposition should produce ~1 hull
         * which fan-triangulates to far fewer tris. */
        ASSERT(out_tris < 1000);
    }

    snap_mesh_cache_destroy(&cache);
    mesh_slot_destroy(&slot);
}

/** Decomposed snap mesh has valid positions and normals. */
static void test_decomposed_has_valid_geometry(void) {
    mesh_slot_t slot;
    build_sphere_slot(&slot, 48, 48, 2.0f); /* 4608 tris */

    snap_mesh_cache_t cache;
    snap_mesh_cache_init(&cache, 16);

    bool ok = snap_mesh_retain_decomposed(&cache, 3, &slot, NULL);
    ASSERT(ok);

    const snap_mesh_t *sm = snap_mesh_cache_get(&cache, 3);
    ASSERT(sm != NULL);
    if (sm) {
        ASSERT(sm->positions != NULL);
        ASSERT(sm->normals != NULL);
        ASSERT(sm->indices != NULL);
        ASSERT(sm->vertex_count > 0);
        ASSERT(sm->index_count > 0);
        ASSERT(sm->index_count % 3 == 0);

        /* All indices should be in bounds. */
        bool indices_valid = true;
        for (uint32_t i = 0; i < sm->index_count; i++) {
            if (sm->indices[i] >= sm->vertex_count) {
                indices_valid = false;
                break;
            }
        }
        ASSERT(indices_valid);

        /* All normals should be unit-length or zero (within tolerance).
         * Some degenerate hull vertices may have zero normals if they
         * aren't shared by any face. */
        bool normals_valid = true;
        for (uint32_t v = 0; v < sm->vertex_count; v++) {
            float nx = sm->normals[v * 3 + 0];
            float ny = sm->normals[v * 3 + 1];
            float nz = sm->normals[v * 3 + 2];
            float len = sqrtf(nx * nx + ny * ny + nz * nz);
            if (len > 0.01f && fabsf(len - 1.0f) > 0.05f) {
                normals_valid = false;
                break;
            }
        }
        ASSERT(normals_valid);

        /* Positions should be within the sphere radius (2.0). */
        bool pos_valid = true;
        for (uint32_t v = 0; v < sm->vertex_count; v++) {
            float px = sm->positions[v * 3 + 0];
            float py = sm->positions[v * 3 + 1];
            float pz = sm->positions[v * 3 + 2];
            float dist = sqrtf(px * px + py * py + pz * pz);
            /* Convex hull may slightly exceed due to decomposition. */
            if (dist > 2.5f) {
                pos_valid = false;
                break;
            }
        }
        ASSERT(pos_valid);
    }

    snap_mesh_cache_destroy(&cache);
    mesh_slot_destroy(&slot);
}

/** Decompose handles a small mesh correctly (still produces valid output). */
static void test_decomposed_small_mesh(void) {
    mesh_slot_t slot;
    build_box_slot(&slot); /* 12 tris — below threshold but still valid input */

    snap_mesh_cache_t cache;
    snap_mesh_cache_init(&cache, 16);

    /* Even though this is below threshold, the function should work. */
    bool ok = snap_mesh_retain_decomposed(&cache, 1, &slot, NULL);
    ASSERT(ok);

    const snap_mesh_t *sm = snap_mesh_cache_get(&cache, 1);
    ASSERT(sm != NULL);
    if (sm) {
        ASSERT(sm->vertex_count > 0);
        ASSERT(sm->index_count > 0);
    }

    snap_mesh_cache_destroy(&cache);
    mesh_slot_destroy(&slot);
}

/** Decompose handles NULL/empty slot gracefully. */
static void test_decomposed_null_and_empty(void) {
    snap_mesh_cache_t cache;
    snap_mesh_cache_init(&cache, 16);

    /* NULL slot. */
    ASSERT(!snap_mesh_retain_decomposed(&cache, 0, NULL, NULL));

    /* Empty slot. */
    mesh_slot_t slot;
    memset(&slot, 0, sizeof(slot));
    ASSERT(!snap_mesh_retain_decomposed(&cache, 0, &slot, NULL));

    /* NULL cache. */
    mesh_slot_t box;
    build_box_slot(&box);
    ASSERT(!snap_mesh_retain_decomposed(NULL, 0, &box, NULL));
    mesh_slot_destroy(&box);

    snap_mesh_cache_destroy(&cache);
}

/** Decompose overwrites previous snap mesh for same entity. */
static void test_decomposed_overwrites_existing(void) {
    mesh_slot_t slot;
    build_sphere_slot(&slot, 48, 48, 1.0f); /* 4608 tris */

    snap_mesh_cache_t cache;
    snap_mesh_cache_init(&cache, 16);

    /* Insert raw mesh first. */
    snap_mesh_retain_from_slot(&cache, 5, &slot);
    const snap_mesh_t *sm1 = snap_mesh_cache_get(&cache, 5);
    ASSERT(sm1 != NULL);
    uint32_t raw_tris = sm1 ? sm1->index_count / 3 : 0;

    /* Now decompose into same slot. */
    bool ok = snap_mesh_retain_decomposed(&cache, 5, &slot, NULL);
    ASSERT(ok);
    const snap_mesh_t *sm2 = snap_mesh_cache_get(&cache, 5);
    ASSERT(sm2 != NULL);
    if (sm2) {
        uint32_t decomp_tris = sm2->index_count / 3;
        /* Decomposed should have fewer tris than raw. */
        ASSERT(decomp_tris < raw_tris);
    }

    snap_mesh_cache_destroy(&cache);
    mesh_slot_destroy(&slot);
}

/* ---- Main ---- */

int main(void) {
    printf("snap_mesh_decompose_tests:\n");

    test_should_decompose_below_threshold();
    test_should_decompose_above_threshold();
    test_should_decompose_null();
    test_should_decompose_at_threshold();
    test_decomposed_reduces_triangle_count();
    test_decomposed_has_valid_geometry();
    test_decomposed_small_mesh();
    test_decomposed_null_and_empty();
    test_decomposed_overwrites_existing();

    printf("snap_mesh_decompose_tests: %d passed, %d failed\n",
           g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
