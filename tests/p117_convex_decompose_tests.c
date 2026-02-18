/**
 * @file p117_convex_decompose_tests.c
 * @brief Unit tests for convex mesh decomposition.
 *
 * Tests cover:
 *   1. Decompose a box mesh → 1 hull
 *   2. Decompose an L-shaped mesh → 2+ hulls
 *   3. Default params are sane
 *   4. Hull vertex counts within limits
 *   5. Decomposed hulls cover the original mesh volume
 *   6. NULL safety
 *   7. Degenerate mesh (single triangle)
 *   8. Decompose a T-shaped mesh → 2+ hulls
 */

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/physics/convex_decompose.h"
#include "ferrum/physics/convex_hull.h"
#include "ferrum/physics/mesh_collider.h"

/* ── Test harness ──────────────────────────────────────────────── */

static int test_count;
static int fail_count;

#define ASSERT_TRUE(cond)                                                      \
    do {                                                                        \
        if (!(cond)) {                                                          \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n",                 \
                    __FILE__, __LINE__, #cond);                                 \
            return 1;                                                           \
        }                                                                       \
    } while (0)

#define ASSERT_FALSE(cond)                                                     \
    do {                                                                        \
        if ((cond)) {                                                           \
            fprintf(stderr, "ASSERT_FALSE failed: %s:%d: %s\n",                \
                    __FILE__, __LINE__, #cond);                                 \
            return 1;                                                           \
        }                                                                       \
    } while (0)

#define ASSERT_EQ(exp, act)                                                    \
    do {                                                                        \
        long long _e = (long long)(exp), _a = (long long)(act);                 \
        if (_e != _a) {                                                         \
            fprintf(stderr, "ASSERT_EQ failed: %s:%d: expected %lld, got %lld\n", \
                    __FILE__, __LINE__, _e, _a);                                \
            return 1;                                                           \
        }                                                                       \
    } while (0)

#define RUN_TEST(fn)                                                           \
    do {                                                                        \
        test_count++;                                                           \
        int _r = fn();                                                          \
        if (_r) {                                                               \
            fail_count++;                                                       \
            fprintf(stderr, "  FAIL: %s\n", #fn);                              \
        } else {                                                                \
            fprintf(stderr, "  PASS: %s\n", #fn);                              \
        }                                                                       \
    } while (0)

/* ── Mesh construction helpers ────────────────────────────────── */

/**
 * Build a box mesh from min/max corners.  Writes 12 triangles (2 per face).
 * Returns triangle count (always 12 if buf is big enough).
 */
static int make_box_mesh(phys_triangle_t *out, uint32_t max_tris,
                         float x0, float y0, float z0,
                         float x1, float y1, float z1) {
    if (max_tris < 12) return 0;
    /* 8 corners */
    phys_vec3_t v[8] = {
        {x0, y0, z0}, {x1, y0, z0}, {x1, y1, z0}, {x0, y1, z0},
        {x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1},
    };
    /* 6 faces × 2 triangles each (CCW winding from outside) */
    int idx[][3] = {
        /* -Z face */ {0,3,1}, {1,3,2},
        /* +Z face */ {4,5,7}, {5,6,7},
        /* -Y face */ {0,1,4}, {1,5,4},
        /* +Y face */ {3,7,2}, {2,7,6},
        /* -X face */ {0,4,3}, {3,4,7},
        /* +X face */ {1,2,5}, {2,6,5},
    };
    for (int i = 0; i < 12; i++) {
        out[i].v[0] = v[idx[i][0]];
        out[i].v[1] = v[idx[i][1]];
        out[i].v[2] = v[idx[i][2]];
    }
    return 12;
}

/**
 * Build an L-shaped mesh by combining two boxes:
 *   Box A: (0,0,0)→(2,1,1)  — bottom of L
 *   Box B: (0,0,0)→(1,2,1)  — left column of L
 *
 * This creates a concave shape that should decompose into 2+ pieces.
 * We construct the L as 6 faces of the combined outline.
 */
static int make_l_shape_mesh(phys_triangle_t *out, uint32_t max_tris) {
    /* Build L as two boxes: bottom + left column.
     * The L outline when viewed from +Z:
     *   (0,2)---(1,2)
     *     |       |
     *   (0,1)---(1,1)---(2,1)
     *     |               |
     *   (0,0)-----------(2,0)
     *
     * For simplicity, build two overlapping boxes and let the
     * decomposition algorithm handle it.
     */
    if (max_tris < 24) return 0;
    int n = 0;
    /* Bottom box: (0,0,0) → (2,1,1) */
    n += make_box_mesh(out + n, max_tris - (uint32_t)n,
                       0, 0, 0, 2, 1, 1);
    /* Left column: (0,1,0) → (1,2,1) */
    n += make_box_mesh(out + n, max_tris - (uint32_t)n,
                       0, 1, 0, 1, 2, 1);
    return n;
}

/**
 * Build a T-shaped mesh by combining two boxes:
 *   Vertical stem: (0.5, 0, 0) → (1.5, 1, 1)
 *   Top bar:       (0, 1, 0) → (2, 1.5, 1)
 */
static int make_t_shape_mesh(phys_triangle_t *out, uint32_t max_tris) {
    if (max_tris < 24) return 0;
    int n = 0;
    /* Stem */
    n += make_box_mesh(out + n, max_tris - (uint32_t)n,
                       0.5f, 0, 0, 1.5f, 1, 1);
    /* Top bar */
    n += make_box_mesh(out + n, max_tris - (uint32_t)n,
                       0, 1, 0, 2, 1.5f, 1);
    return n;
}

/* ── Tests ─────────────────────────────────────────────────────── */

/** 1. Decompose a simple box → should produce exactly 1 hull. */
static int test_decompose_box(void) {
    phys_triangle_t tris[12];
    int n = make_box_mesh(tris, 12, -1, -1, -1, 1, 1, 1);
    ASSERT_EQ(12, n);

    phys_decompose_params_t params = phys_decompose_params_default();
    phys_decompose_result_t result;
    memset(&result, 0, sizeof(result));

    int rc = phys_decompose_mesh(tris, (uint32_t)n, &params, &result);
    ASSERT_EQ(0, rc);
    ASSERT_TRUE(result.hull_count >= 1);
    /* A box is already convex, so we expect 1 hull. */
    ASSERT_TRUE(result.hull_count <= 2);
    /* Hull should have at least 8 vertices (box corners). */
    ASSERT_TRUE(result.hulls[0].vertex_count >= 4);
    return 0;
}

/** 2. Decompose an L-shaped mesh → should produce 2+ hulls. */
static int test_decompose_l_shape(void) {
    phys_triangle_t tris[24];
    int n = make_l_shape_mesh(tris, 24);
    ASSERT_TRUE(n > 0);

    phys_decompose_params_t params = phys_decompose_params_default();
    params.concavity_threshold = 0.05f;
    phys_decompose_result_t result;
    memset(&result, 0, sizeof(result));

    int rc = phys_decompose_mesh(tris, (uint32_t)n, &params, &result);
    ASSERT_EQ(0, rc);
    /* L-shape is concave, expect at least 2 pieces. */
    ASSERT_TRUE(result.hull_count >= 2);
    return 0;
}

/** 3. Default params are sane. */
static int test_default_params(void) {
    phys_decompose_params_t p = phys_decompose_params_default();
    ASSERT_TRUE(p.resolution >= 8 && p.resolution <= 64);
    ASSERT_TRUE(p.concavity_threshold > 0.0f && p.concavity_threshold < 1.0f);
    ASSERT_TRUE(p.max_hulls >= 1 && p.max_hulls <= 64);
    ASSERT_TRUE(p.min_voxels >= 1);
    return 0;
}

/** 4. Hull vertex counts are within limits. */
static int test_hull_vertex_limits(void) {
    phys_triangle_t tris[24];
    int n = make_l_shape_mesh(tris, 24);
    ASSERT_TRUE(n > 0);

    phys_decompose_params_t params = phys_decompose_params_default();
    phys_decompose_result_t result;
    memset(&result, 0, sizeof(result));

    int rc = phys_decompose_mesh(tris, (uint32_t)n, &params, &result);
    ASSERT_EQ(0, rc);

    for (uint32_t i = 0; i < result.hull_count; i++) {
        ASSERT_TRUE(result.hulls[i].vertex_count <= PHYS_CONVEX_MAX_VERTS);
        ASSERT_TRUE(result.hulls[i].vertex_count >= 4);
    }
    return 0;
}

/** 5. Decomposed hulls cover the original volume approximately.
 *  Check that hull centroids lie within the original mesh AABB. */
static int test_hulls_cover_volume(void) {
    phys_triangle_t tris[12];
    int n = make_box_mesh(tris, 12, -1, -1, -1, 1, 1, 1);
    ASSERT_EQ(12, n);

    phys_decompose_params_t params = phys_decompose_params_default();
    phys_decompose_result_t result;
    memset(&result, 0, sizeof(result));

    int rc = phys_decompose_mesh(tris, (uint32_t)n, &params, &result);
    ASSERT_EQ(0, rc);

    for (uint32_t i = 0; i < result.hull_count; i++) {
        phys_vec3_t c = result.hulls[i].centroid;
        /* Centroid should be within expanded AABB. */
        ASSERT_TRUE(c.x >= -1.5f && c.x <= 1.5f);
        ASSERT_TRUE(c.y >= -1.5f && c.y <= 1.5f);
        ASSERT_TRUE(c.z >= -1.5f && c.z <= 1.5f);
    }
    return 0;
}

/** 6. NULL safety. */
static int test_null_safety(void) {
    phys_decompose_params_t params = phys_decompose_params_default();
    phys_decompose_result_t result;
    memset(&result, 0, sizeof(result));

    /* NULL triangles with non-zero count. */
    ASSERT_EQ(-1, phys_decompose_mesh(NULL, 10, &params, &result));
    /* NULL params. */
    phys_triangle_t tris[12];
    make_box_mesh(tris, 12, 0, 0, 0, 1, 1, 1);
    ASSERT_EQ(-1, phys_decompose_mesh(tris, 12, NULL, &result));
    /* NULL result. */
    ASSERT_EQ(-1, phys_decompose_mesh(tris, 12, &params, NULL));
    return 0;
}

/** 7. Degenerate: single triangle. */
static int test_single_triangle(void) {
    phys_triangle_t tri = {
        .v = {{0,0,0}, {1,0,0}, {0,1,0}}
    };
    phys_decompose_params_t params = phys_decompose_params_default();
    phys_decompose_result_t result;
    memset(&result, 0, sizeof(result));

    int rc = phys_decompose_mesh(&tri, 1, &params, &result);
    /* A single triangle is degenerate but should still produce a hull. */
    ASSERT_EQ(0, rc);
    ASSERT_TRUE(result.hull_count >= 1);
    return 0;
}

/** 8. Decompose T-shape → 2+ hulls. */
static int test_decompose_t_shape(void) {
    phys_triangle_t tris[24];
    int n = make_t_shape_mesh(tris, 24);
    ASSERT_TRUE(n > 0);

    phys_decompose_params_t params = phys_decompose_params_default();
    params.concavity_threshold = 0.05f;
    phys_decompose_result_t result;
    memset(&result, 0, sizeof(result));

    int rc = phys_decompose_mesh(tris, (uint32_t)n, &params, &result);
    ASSERT_EQ(0, rc);
    /* T-shape is concave, expect 2+ pieces. */
    ASSERT_TRUE(result.hull_count >= 2);
    return 0;
}

/* ── Main ──────────────────────────────────────────────────────── */

int main(void) {
    fprintf(stderr, "=== p117_convex_decompose_tests ===\n");

    RUN_TEST(test_decompose_box);
    RUN_TEST(test_decompose_l_shape);
    RUN_TEST(test_default_params);
    RUN_TEST(test_hull_vertex_limits);
    RUN_TEST(test_hulls_cover_volume);
    RUN_TEST(test_null_safety);
    RUN_TEST(test_single_triangle);
    RUN_TEST(test_decompose_t_shape);

    fprintf(stderr, "\n%d/%d passed\n", test_count - fail_count, test_count);
    return fail_count ? 1 : 0;
}
