/**
 * @file p115_gjk_epa_tests.c
 * @brief Unit tests for GJK intersection and EPA penetration depth.
 *
 * Tests cover:
 *   1. Two separated spheres — no intersection, correct distance
 *   2. Two overlapping spheres — intersection, EPA gives depth/normal
 *   3. Sphere vs box separated — no intersection
 *   4. Sphere vs box overlapping — intersection + penetration
 *   5. Two identical spheres (full overlap) — deep penetration
 *   6. Two convex hulls separated — no intersection
 *   7. Two convex hulls overlapping — intersection + EPA
 *   8. Touching spheres (zero gap) — intersection
 *   9. NULL safety
 *  10. Sphere vs convex hull — cross-shape test
 */

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/physics/gjk_epa.h"
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

#define ASSERT_FALSE(cond)                                                     \
    do {                                                                        \
        if ((cond)) {                                                           \
            fprintf(stderr, "ASSERT_FALSE failed: %s:%d: %s\n",                \
                    __FILE__, __LINE__, #cond);                                 \
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

/* ── Support function implementations ─────────────────────────── */

/** Sphere shape data for support function. */
typedef struct test_sphere {
    phys_vec3_t center;
    float radius;
} test_sphere_t;

/** Sphere support: center + radius * normalize(dir). */
static phys_vec3_t sphere_support(const void *data, phys_vec3_t dir) {
    const test_sphere_t *s = data;
    float len = sqrtf(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
    if (len < 1e-10f) {
        return s->center;
    }
    float inv = s->radius / len;
    return (phys_vec3_t){
        s->center.x + dir.x * inv,
        s->center.y + dir.y * inv,
        s->center.z + dir.z * inv,
    };
}

/** Box shape data for support function. */
typedef struct test_box {
    phys_vec3_t center;
    phys_vec3_t half_extents;
} test_box_t;

/** Axis-aligned box support: center + sign(dir) * half_extents. */
static phys_vec3_t box_support(const void *data, phys_vec3_t dir) {
    const test_box_t *b = data;
    return (phys_vec3_t){
        b->center.x + (dir.x >= 0 ? b->half_extents.x : -b->half_extents.x),
        b->center.y + (dir.y >= 0 ? b->half_extents.y : -b->half_extents.y),
        b->center.z + (dir.z >= 0 ? b->half_extents.z : -b->half_extents.z),
    };
}

/** Convex hull shape data (hull in world space for simplicity). */
typedef struct test_hull_shape {
    const phys_convex_hull_t *hull;
    phys_vec3_t offset; /**< World-space translation. */
} test_hull_shape_t;

/** Convex hull support: hull support + offset. */
static phys_vec3_t hull_support(const void *data, phys_vec3_t dir) {
    const test_hull_shape_t *hs = data;
    phys_vec3_t local = phys_convex_hull_support(hs->hull, dir);
    return (phys_vec3_t){
        local.x + hs->offset.x,
        local.y + hs->offset.y,
        local.z + hs->offset.z,
    };
}

/* ── Helper ────────────────────────────────────────────────────── */

static phys_vec3_t v3(float x, float y, float z) {
    return (phys_vec3_t){x, y, z};
}

/* ── Tests ─────────────────────────────────────────────────────── */

/** Two spheres clearly separated along X axis. */
static int test_spheres_separated(void) {
    test_sphere_t a = {{0, 0, 0}, 1.0f};
    test_sphere_t b = {{5, 0, 0}, 1.0f};

    phys_gjk_result_t result;
    memset(&result, 0, sizeof(result));

    bool hit = phys_gjk_intersect(sphere_support, &a, sphere_support, &b, &result);
    ASSERT_FALSE(hit);
    ASSERT_FALSE(result.intersecting);
    /* Distance should be ~3.0 (5 - 1 - 1). */
    ASSERT_FLOAT_NEAR(3.0f, result.distance, 0.1f);
    return 0;
}

/** Two spheres overlapping. */
static int test_spheres_overlapping(void) {
    test_sphere_t a = {{0, 0, 0}, 1.0f};
    test_sphere_t b = {{1, 0, 0}, 1.0f};  /* overlap by 1.0 */

    phys_gjk_result_t result;
    memset(&result, 0, sizeof(result));

    bool hit = phys_gjk_intersect(sphere_support, &a, sphere_support, &b, &result);
    ASSERT_TRUE(hit);
    ASSERT_TRUE(result.intersecting);

    /* Run EPA for penetration depth. */
    bool epa_ok = phys_epa_penetration(sphere_support, &a, sphere_support, &b, &result);
    ASSERT_TRUE(epa_ok);
    /* Penetration should be ~1.0 (r1 + r2 - dist = 1 + 1 - 1 = 1). */
    ASSERT_FLOAT_NEAR(1.0f, result.penetration, 0.15f);
    /* Normal should point roughly along +X (from A to B). */
    ASSERT_TRUE(result.normal.x > 0.5f);
    return 0;
}

/** Sphere vs axis-aligned box, separated. */
static int test_sphere_vs_box_separated(void) {
    test_sphere_t s = {{0, 0, 0}, 1.0f};
    test_box_t b = {{5, 0, 0}, {1, 1, 1}};

    phys_gjk_result_t result;
    memset(&result, 0, sizeof(result));

    bool hit = phys_gjk_intersect(sphere_support, &s, box_support, &b, &result);
    ASSERT_FALSE(hit);
    /* Distance: sphere surface at x=1, box surface at x=4 → gap ~3.
       GJK distance is approximate for curved shapes. */
    ASSERT_FLOAT_NEAR(3.0f, result.distance, 0.5f);
    return 0;
}

/** Sphere vs axis-aligned box, overlapping. */
static int test_sphere_vs_box_overlapping(void) {
    test_sphere_t s = {{0, 0, 0}, 1.0f};
    test_box_t b = {{1.5f, 0, 0}, {1, 1, 1}};  /* box from 0.5 to 2.5 */

    phys_gjk_result_t result;
    memset(&result, 0, sizeof(result));

    bool hit = phys_gjk_intersect(sphere_support, &s, box_support, &b, &result);
    ASSERT_TRUE(hit);

    bool epa_ok = phys_epa_penetration(sphere_support, &s, box_support, &b, &result);
    ASSERT_TRUE(epa_ok);
    /* Sphere extends to x=1, box starts at x=0.5 → overlap ~0.5. */
    ASSERT_TRUE(result.penetration > 0.1f);
    ASSERT_TRUE(result.penetration < 1.5f);
    return 0;
}

/** Two identical spheres at same position — deep overlap. */
static int test_spheres_coincident(void) {
    test_sphere_t a = {{0, 0, 0}, 1.0f};
    test_sphere_t b = {{0, 0, 0}, 1.0f};

    phys_gjk_result_t result;
    memset(&result, 0, sizeof(result));

    bool hit = phys_gjk_intersect(sphere_support, &a, sphere_support, &b, &result);
    ASSERT_TRUE(hit);

    bool epa_ok = phys_epa_penetration(sphere_support, &a, sphere_support, &b, &result);
    ASSERT_TRUE(epa_ok);
    /* Penetration should be ~2.0 (diameter). */
    ASSERT_FLOAT_NEAR(2.0f, result.penetration, 0.3f);
    return 0;
}

/** Two convex hulls (tetrahedra) separated. */
static int test_hulls_separated(void) {
    phys_convex_hull_t hull_a, hull_b;
    memset(&hull_a, 0, sizeof(hull_a));
    memset(&hull_b, 0, sizeof(hull_b));

    phys_vec3_t pts_a[4] = {v3(0,0,0), v3(1,0,0), v3(0,1,0), v3(0,0,1)};
    phys_vec3_t pts_b[4] = {v3(0,0,0), v3(1,0,0), v3(0,1,0), v3(0,0,1)};
    phys_convex_hull_build(&hull_a, pts_a, 4);
    phys_convex_hull_build(&hull_b, pts_b, 4);

    test_hull_shape_t a = {&hull_a, v3(0, 0, 0)};
    test_hull_shape_t b = {&hull_b, v3(5, 0, 0)};  /* far apart */

    phys_gjk_result_t result;
    memset(&result, 0, sizeof(result));

    bool hit = phys_gjk_intersect(hull_support, &a, hull_support, &b, &result);
    ASSERT_FALSE(hit);
    ASSERT_TRUE(result.distance > 3.0f);
    return 0;
}

/** Two convex hulls overlapping. */
static int test_hulls_overlapping(void) {
    phys_convex_hull_t hull_a, hull_b;
    memset(&hull_a, 0, sizeof(hull_a));
    memset(&hull_b, 0, sizeof(hull_b));

    phys_vec3_t pts_a[4] = {v3(0,0,0), v3(2,0,0), v3(0,2,0), v3(0,0,2)};
    phys_vec3_t pts_b[4] = {v3(0,0,0), v3(2,0,0), v3(0,2,0), v3(0,0,2)};
    phys_convex_hull_build(&hull_a, pts_a, 4);
    phys_convex_hull_build(&hull_b, pts_b, 4);

    test_hull_shape_t a = {&hull_a, v3(0, 0, 0)};
    test_hull_shape_t b = {&hull_b, v3(0.5f, 0, 0)};  /* slight overlap */

    phys_gjk_result_t result;
    memset(&result, 0, sizeof(result));

    bool hit = phys_gjk_intersect(hull_support, &a, hull_support, &b, &result);
    ASSERT_TRUE(hit);

    bool epa_ok = phys_epa_penetration(hull_support, &a, hull_support, &b, &result);
    ASSERT_TRUE(epa_ok);
    ASSERT_TRUE(result.penetration > 0.0f);
    return 0;
}

/** Two spheres just touching (gap = 0). */
static int test_spheres_touching(void) {
    test_sphere_t a = {{0, 0, 0}, 1.0f};
    test_sphere_t b = {{2, 0, 0}, 1.0f};  /* exactly touching */

    phys_gjk_result_t result;
    memset(&result, 0, sizeof(result));

    bool hit = phys_gjk_intersect(sphere_support, &a, sphere_support, &b, &result);
    /* Touching counts as intersecting (or distance ≈ 0). */
    if (hit) {
        ASSERT_TRUE(result.intersecting);
    } else {
        ASSERT_FLOAT_NEAR(0.0f, result.distance, 0.05f);
    }
    return 0;
}

/** NULL safety — should not crash. */
static int test_null_safety(void) {
    test_sphere_t s = {{0, 0, 0}, 1.0f};
    phys_gjk_result_t result;
    memset(&result, 0, sizeof(result));

    bool hit = phys_gjk_intersect(NULL, &s, sphere_support, &s, &result);
    ASSERT_FALSE(hit);

    hit = phys_gjk_intersect(sphere_support, &s, NULL, &s, &result);
    ASSERT_FALSE(hit);

    hit = phys_gjk_intersect(sphere_support, &s, sphere_support, &s, NULL);
    ASSERT_FALSE(hit);

    return 0;
}

/** Sphere vs convex hull — cross-shape type. */
static int test_sphere_vs_hull(void) {
    test_sphere_t s = {{0, 0, 0}, 1.0f};

    phys_convex_hull_t hull;
    memset(&hull, 0, sizeof(hull));
    /* Unit cube hull centered at (4, 0, 0). */
    phys_vec3_t pts[8] = {
        v3(-1,-1,-1), v3(1,-1,-1), v3(1,1,-1), v3(-1,1,-1),
        v3(-1,-1, 1), v3(1,-1, 1), v3(1,1, 1), v3(-1,1, 1),
    };
    phys_convex_hull_build(&hull, pts, 8);
    test_hull_shape_t hs = {&hull, v3(4, 0, 0)};

    phys_gjk_result_t result;
    memset(&result, 0, sizeof(result));

    /* Sphere at origin, hull at (4,0,0) with extent 1 → gap = 4-1-1 = 2.
       GJK distance is approximate for curved shapes. */
    bool hit = phys_gjk_intersect(sphere_support, &s, hull_support, &hs, &result);
    ASSERT_FALSE(hit);
    ASSERT_FLOAT_NEAR(2.0f, result.distance, 0.6f);

    /* Move hull closer to overlap. */
    hs.offset = v3(1.5f, 0, 0);  /* hull from 0.5 to 2.5 → overlaps sphere */
    memset(&result, 0, sizeof(result));

    hit = phys_gjk_intersect(sphere_support, &s, hull_support, &hs, &result);
    ASSERT_TRUE(hit);

    bool epa_ok = phys_epa_penetration(sphere_support, &s, hull_support, &hs, &result);
    ASSERT_TRUE(epa_ok);
    ASSERT_TRUE(result.penetration > 0.0f);
    return 0;
}

/* ── Runner ────────────────────────────────────────────────────── */

int main(void) {
    printf("p115_gjk_epa_tests:\n");

    RUN_TEST(test_spheres_separated);
    RUN_TEST(test_spheres_overlapping);
    RUN_TEST(test_sphere_vs_box_separated);
    RUN_TEST(test_sphere_vs_box_overlapping);
    RUN_TEST(test_spheres_coincident);
    RUN_TEST(test_hulls_separated);
    RUN_TEST(test_hulls_overlapping);
    RUN_TEST(test_spheres_touching);
    RUN_TEST(test_null_safety);
    RUN_TEST(test_sphere_vs_hull);

    printf("\n%d/%d tests passed\n", test_count - fail_count, test_count);
    return fail_count ? 1 : 0;
}
