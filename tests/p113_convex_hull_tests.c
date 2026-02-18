/**
 * @file p113_convex_hull_tests.c
 * @brief Unit tests for convex hull construction and support function.
 *
 * Tests cover:
 *   1. Support function on a unit cube (8 vertices, 6 faces)
 *   2. Support function returns correct vertex for axis-aligned directions
 *   3. Support function with diagonal direction
 *   4. Build hull from tetrahedron point cloud
 *   5. Build hull from cube point cloud (8 points → 8 verts, 6 faces)
 *   6. Build hull from coplanar points (degenerate → error)
 *   7. Build hull from collinear points (degenerate → error)
 *   8. Build hull NULL safety
 *   9. Build hull exceeding max verts → error
 *  10. AABB computation (local space)
 *  11. World-space AABB with rotation
 *  12. Recompute bounds after vertex modification
 */

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/physics/convex_hull.h"

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

#define ASSERT_INT_EQ(exp, act)                                                \
    do {                                                                        \
        int _e = (exp), _a = (act);                                             \
        if (_e != _a) {                                                         \
            fprintf(stderr, "ASSERT_INT_EQ failed: %s:%d: expected %d got %d\n",\
                    __FILE__, __LINE__, _e, _a);                                \
            return 1;                                                           \
        }                                                                       \
    } while (0)

#define ASSERT_UINT_EQ(exp, act)                                               \
    do {                                                                        \
        uint32_t _e = (exp), _a = (act);                                        \
        if (_e != _a) {                                                         \
            fprintf(stderr, "ASSERT_UINT_EQ failed: %s:%d: expected %u got %u\n",\
                    __FILE__, __LINE__, _e, _a);                                \
            return 1;                                                           \
        }                                                                       \
    } while (0)

#define ASSERT_FLOAT_NEAR(exp, act, eps)                                       \
    do {                                                                        \
        float _e = (exp), _a = (act);                                           \
        if (fabsf(_e - _a) > (eps)) {                                           \
            fprintf(stderr, "ASSERT_FLOAT_NEAR failed: %s:%d: expected %.6f got %.6f (eps=%.6f)\n", \
                    __FILE__, __LINE__, (double)_e, (double)_a, (double)(eps)); \
            return 1;                                                           \
        }                                                                       \
    } while (0)

#define RUN_TEST(fn)                                                           \
    do {                                                                        \
        printf("  %-60s", #fn);                                                 \
        int _r = fn();                                                          \
        if (_r) { printf("[FAIL]\n"); fail_count++; }                           \
        else    { printf("[OK]\n"); }                                           \
        test_count++;                                                           \
    } while (0)

/* ── Helpers ───────────────────────────────────────────────────── */

static phys_vec3_t v3(float x, float y, float z) {
    return (phys_vec3_t){x, y, z};
}

/** Build a unit cube hull manually for support function tests. */
static void make_unit_cube(phys_convex_hull_t *hull) {
    memset(hull, 0, sizeof(*hull));
    /* 8 vertices of a unit cube centered at origin. */
    hull->vertices[0] = v3(-1, -1, -1);
    hull->vertices[1] = v3( 1, -1, -1);
    hull->vertices[2] = v3( 1,  1, -1);
    hull->vertices[3] = v3(-1,  1, -1);
    hull->vertices[4] = v3(-1, -1,  1);
    hull->vertices[5] = v3( 1, -1,  1);
    hull->vertices[6] = v3( 1,  1,  1);
    hull->vertices[7] = v3(-1,  1,  1);
    hull->vertex_count = 8;
    hull->centroid = v3(0, 0, 0);
    hull->aabb.min = v3(-1, -1, -1);
    hull->aabb.max = v3( 1,  1,  1);
}

/* ── Tests ─────────────────────────────────────────────────────── */

/** Support function returns vertex with max dot product along +X. */
static int test_support_positive_x(void) {
    phys_convex_hull_t hull;
    make_unit_cube(&hull);

    phys_vec3_t s = phys_convex_hull_support(&hull, v3(1, 0, 0));
    /* Should be one of the +X vertices (x == 1). */
    ASSERT_FLOAT_NEAR(1.0f, s.x, 0.001f);
    return 0;
}

/** Support function returns vertex with max dot product along -Y. */
static int test_support_negative_y(void) {
    phys_convex_hull_t hull;
    make_unit_cube(&hull);

    phys_vec3_t s = phys_convex_hull_support(&hull, v3(0, -1, 0));
    ASSERT_FLOAT_NEAR(-1.0f, s.y, 0.001f);
    return 0;
}

/** Support function with diagonal direction picks corner vertex. */
static int test_support_diagonal(void) {
    phys_convex_hull_t hull;
    make_unit_cube(&hull);

    phys_vec3_t s = phys_convex_hull_support(&hull, v3(1, 1, 1));
    /* Should be vertex (1, 1, 1). */
    ASSERT_FLOAT_NEAR(1.0f, s.x, 0.001f);
    ASSERT_FLOAT_NEAR(1.0f, s.y, 0.001f);
    ASSERT_FLOAT_NEAR(1.0f, s.z, 0.001f);
    return 0;
}

/** Build hull from tetrahedron (4 points). */
static int test_build_tetrahedron(void) {
    phys_convex_hull_t hull;
    memset(&hull, 0, sizeof(hull));

    phys_vec3_t pts[4] = {
        v3(0, 0, 0),
        v3(1, 0, 0),
        v3(0, 1, 0),
        v3(0, 0, 1),
    };

    int rc = phys_convex_hull_build(&hull, pts, 4);
    ASSERT_INT_EQ(0, rc);
    ASSERT_UINT_EQ(4, hull.vertex_count);
    ASSERT_UINT_EQ(4, hull.face_count);  /* tetrahedron has 4 triangular faces */

    /* Centroid should be mean of vertices: (0.25, 0.25, 0.25). */
    ASSERT_FLOAT_NEAR(0.25f, hull.centroid.x, 0.01f);
    ASSERT_FLOAT_NEAR(0.25f, hull.centroid.y, 0.01f);
    ASSERT_FLOAT_NEAR(0.25f, hull.centroid.z, 0.01f);

    /* AABB should contain all points. */
    ASSERT_TRUE(hull.aabb.min.x <= 0.0f);
    ASSERT_TRUE(hull.aabb.min.y <= 0.0f);
    ASSERT_TRUE(hull.aabb.min.z <= 0.0f);
    ASSERT_TRUE(hull.aabb.max.x >= 1.0f);
    ASSERT_TRUE(hull.aabb.max.y >= 1.0f);
    ASSERT_TRUE(hull.aabb.max.z >= 1.0f);

    /* All face normals should point outward (dot with centroid→face_center > 0). */
    for (uint32_t f = 0; f < hull.face_count; f++) {
        phys_convex_face_t *face = &hull.faces[f];
        /* Compute face center. */
        phys_vec3_t center = v3(0, 0, 0);
        for (uint16_t i = 0; i < face->index_count; i++) {
            uint16_t vi = hull.indices[face->index_start + i];
            center.x += hull.vertices[vi].x;
            center.y += hull.vertices[vi].y;
            center.z += hull.vertices[vi].z;
        }
        center.x /= face->index_count;
        center.y /= face->index_count;
        center.z /= face->index_count;
        /* Normal should point away from centroid. */
        phys_vec3_t to_face = vec3_sub(center, hull.centroid);
        float d = vec3_dot(to_face, face->normal);
        ASSERT_TRUE(d > 0.0f);
    }

    return 0;
}

/** Build hull from 8 cube points. */
static int test_build_cube(void) {
    phys_convex_hull_t hull;
    memset(&hull, 0, sizeof(hull));

    phys_vec3_t pts[8] = {
        v3(-1, -1, -1), v3( 1, -1, -1),
        v3( 1,  1, -1), v3(-1,  1, -1),
        v3(-1, -1,  1), v3( 1, -1,  1),
        v3( 1,  1,  1), v3(-1,  1,  1),
    };

    int rc = phys_convex_hull_build(&hull, pts, 8);
    ASSERT_INT_EQ(0, rc);
    ASSERT_UINT_EQ(8, hull.vertex_count);
    ASSERT_UINT_EQ(6, hull.face_count);  /* cube has 6 quad faces */

    /* Centroid at origin. */
    ASSERT_FLOAT_NEAR(0.0f, hull.centroid.x, 0.01f);
    ASSERT_FLOAT_NEAR(0.0f, hull.centroid.y, 0.01f);
    ASSERT_FLOAT_NEAR(0.0f, hull.centroid.z, 0.01f);

    /* Support along +X should give x=1. */
    phys_vec3_t s = phys_convex_hull_support(&hull, v3(1, 0, 0));
    ASSERT_FLOAT_NEAR(1.0f, s.x, 0.001f);

    return 0;
}

/** Build hull from duplicate/interior points — should reduce. */
static int test_build_with_interior_points(void) {
    phys_convex_hull_t hull;
    memset(&hull, 0, sizeof(hull));

    /* Cube corners + 4 interior points. */
    phys_vec3_t pts[12] = {
        v3(-1, -1, -1), v3( 1, -1, -1),
        v3( 1,  1, -1), v3(-1,  1, -1),
        v3(-1, -1,  1), v3( 1, -1,  1),
        v3( 1,  1,  1), v3(-1,  1,  1),
        v3(0, 0, 0),    v3(0.5f, 0.5f, 0),
        v3(-0.5f, 0, 0.5f), v3(0, -0.5f, -0.5f),
    };

    int rc = phys_convex_hull_build(&hull, pts, 12);
    ASSERT_INT_EQ(0, rc);
    /* Interior points should be discarded; hull is still a cube. */
    ASSERT_UINT_EQ(8, hull.vertex_count);
    return 0;
}

/** Coplanar points are degenerate (zero volume). */
static int test_build_coplanar_fails(void) {
    phys_convex_hull_t hull;
    memset(&hull, 0, sizeof(hull));

    phys_vec3_t pts[4] = {
        v3(0, 0, 0), v3(1, 0, 0),
        v3(1, 1, 0), v3(0, 1, 0),
    };

    int rc = phys_convex_hull_build(&hull, pts, 4);
    ASSERT_INT_EQ(-1, rc);
    return 0;
}

/** Collinear points are degenerate. */
static int test_build_collinear_fails(void) {
    phys_convex_hull_t hull;
    memset(&hull, 0, sizeof(hull));

    phys_vec3_t pts[3] = {
        v3(0, 0, 0), v3(1, 0, 0), v3(2, 0, 0),
    };

    int rc = phys_convex_hull_build(&hull, pts, 3);
    ASSERT_INT_EQ(-1, rc);
    return 0;
}

/** NULL safety. */
static int test_build_null_safety(void) {
    phys_vec3_t pts[4] = { v3(0,0,0), v3(1,0,0), v3(0,1,0), v3(0,0,1) };

    ASSERT_INT_EQ(-1, phys_convex_hull_build(NULL, pts, 4));

    phys_convex_hull_t hull;
    memset(&hull, 0, sizeof(hull));
    ASSERT_INT_EQ(-1, phys_convex_hull_build(&hull, NULL, 4));
    ASSERT_INT_EQ(-1, phys_convex_hull_build(&hull, pts, 0));

    /* Support on empty hull should return zero. */
    phys_vec3_t s = phys_convex_hull_support(&hull, v3(1, 0, 0));
    ASSERT_FLOAT_NEAR(0.0f, s.x, 0.001f);
    ASSERT_FLOAT_NEAR(0.0f, s.y, 0.001f);
    ASSERT_FLOAT_NEAR(0.0f, s.z, 0.001f);

    return 0;
}

/** Exceeding max verts. */
static int test_build_too_many_verts(void) {
    phys_convex_hull_t hull;
    memset(&hull, 0, sizeof(hull));

    phys_vec3_t pts[PHYS_CONVEX_MAX_VERTS + 1];
    for (uint32_t i = 0; i <= PHYS_CONVEX_MAX_VERTS; i++) {
        pts[i] = v3((float)i, 0, 0);
    }

    int rc = phys_convex_hull_build(&hull, pts, PHYS_CONVEX_MAX_VERTS + 1);
    ASSERT_INT_EQ(-1, rc);
    return 0;
}

/** Local AABB matches vertex extents. */
static int test_local_aabb(void) {
    phys_convex_hull_t hull;
    memset(&hull, 0, sizeof(hull));

    phys_vec3_t pts[4] = {
        v3(0, 0, 0), v3(2, 0, 0), v3(0, 3, 0), v3(0, 0, 4),
    };

    int rc = phys_convex_hull_build(&hull, pts, 4);
    ASSERT_INT_EQ(0, rc);

    ASSERT_FLOAT_NEAR(0.0f, hull.aabb.min.x, 0.001f);
    ASSERT_FLOAT_NEAR(0.0f, hull.aabb.min.y, 0.001f);
    ASSERT_FLOAT_NEAR(0.0f, hull.aabb.min.z, 0.001f);
    ASSERT_FLOAT_NEAR(2.0f, hull.aabb.max.x, 0.001f);
    ASSERT_FLOAT_NEAR(3.0f, hull.aabb.max.y, 0.001f);
    ASSERT_FLOAT_NEAR(4.0f, hull.aabb.max.z, 0.001f);

    return 0;
}

/** World-space AABB with 90° rotation around Y. */
static int test_world_aabb_rotated(void) {
    phys_convex_hull_t hull;
    make_unit_cube(&hull);

    /* 90° rotation around Y: X→Z, Z→-X */
    float a = 3.14159265f / 4.0f;  /* 45° half-angle for 90° rotation */
    phys_quat_t rot = {0, sinf(a), 0, cosf(a)};
    phys_vec3_t pos = v3(10, 20, 30);

    phys_aabb_t waabb = phys_convex_hull_world_aabb(&hull, pos, rot);

    /* After 90° Y rotation, cube extents stay 2×2×2 (symmetric). */
    float size_x = waabb.max.x - waabb.min.x;
    float size_y = waabb.max.y - waabb.min.y;
    float size_z = waabb.max.z - waabb.min.z;

    /* All dimensions should be ~2.0 (unit cube has half-extent 1). */
    ASSERT_TRUE(size_x >= 1.9f && size_x <= 2.1f);
    ASSERT_TRUE(size_y >= 1.9f && size_y <= 2.1f);
    ASSERT_TRUE(size_z >= 1.9f && size_z <= 2.1f);

    /* Center should be at pos. */
    float cx = (waabb.min.x + waabb.max.x) * 0.5f;
    float cy = (waabb.min.y + waabb.max.y) * 0.5f;
    float cz = (waabb.min.z + waabb.max.z) * 0.5f;
    ASSERT_FLOAT_NEAR(10.0f, cx, 0.1f);
    ASSERT_FLOAT_NEAR(20.0f, cy, 0.1f);
    ASSERT_FLOAT_NEAR(30.0f, cz, 0.1f);

    return 0;
}

/** Recompute bounds updates centroid and AABB. */
static int test_recompute_bounds(void) {
    phys_convex_hull_t hull;
    make_unit_cube(&hull);

    /* Shift all vertices by (+5, 0, 0). */
    for (uint32_t i = 0; i < hull.vertex_count; i++) {
        hull.vertices[i].x += 5.0f;
    }

    phys_convex_hull_recompute_bounds(&hull);

    ASSERT_FLOAT_NEAR(5.0f, hull.centroid.x, 0.01f);
    ASSERT_FLOAT_NEAR(4.0f, hull.aabb.min.x, 0.001f);
    ASSERT_FLOAT_NEAR(6.0f, hull.aabb.max.x, 0.001f);

    return 0;
}

/* ── Runner ────────────────────────────────────────────────────── */

int main(void) {
    printf("p113_convex_hull_tests:\n");

    RUN_TEST(test_support_positive_x);
    RUN_TEST(test_support_negative_y);
    RUN_TEST(test_support_diagonal);
    RUN_TEST(test_build_tetrahedron);
    RUN_TEST(test_build_cube);
    RUN_TEST(test_build_with_interior_points);
    RUN_TEST(test_build_coplanar_fails);
    RUN_TEST(test_build_collinear_fails);
    RUN_TEST(test_build_null_safety);
    RUN_TEST(test_build_too_many_verts);
    RUN_TEST(test_local_aabb);
    RUN_TEST(test_world_aabb_rotated);
    RUN_TEST(test_recompute_bounds);

    printf("\n%d/%d tests passed\n", test_count - fail_count, test_count);
    return fail_count ? 1 : 0;
}
