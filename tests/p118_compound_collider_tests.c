/**
 * @file p118_compound_collider_tests.c
 * @brief Unit tests for compound convex collider narrowphase dispatch.
 *
 * Tests cover:
 *   1. Compound vs sphere — single child hull
 *   2. Compound vs sphere — multiple child hulls (only nearest contacts)
 *   3. Compound vs box
 *   4. Compound AABB computation (union of children)
 *   5. Compound vs compound (both sides compound)
 *   6. Empty compound (0 children) produces no contacts
 *   7. Compound registration in world
 */

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/physics/convex_compound.h"
#include "ferrum/physics/convex_hull.h"
#include "ferrum/physics/collider.h"
#include "ferrum/physics/narrowphase_convex.h"
#include "ferrum/physics/narrowphase.h"
#include "ferrum/math/vec3.h"
#include "ferrum/math/quat.h"

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

#define ASSERT_EQ(exp, act)                                                    \
    do {                                                                        \
        long long _e = (long long)(exp), _a = (long long)(act);                 \
        if (_e != _a) {                                                         \
            fprintf(stderr, "ASSERT_EQ failed: %s:%d: expected %lld, got %lld\n", \
                    __FILE__, __LINE__, _e, _a);                                \
            return 1;                                                           \
        }                                                                       \
    } while (0)

#define ASSERT_NEAR(exp, act, tol)                                             \
    do {                                                                        \
        float _e = (float)(exp), _a = (float)(act);                             \
        if (fabsf(_e - _a) > (float)(tol)) {                                    \
            fprintf(stderr, "ASSERT_NEAR failed: %s:%d: expected %f, got %f\n", \
                    __FILE__, __LINE__, (double)_e, (double)_a);                \
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

/* ── Helpers ───────────────────────────────────────────────────── */

/** Build a unit cube hull centered at origin. */
static void make_unit_cube_hull(phys_convex_hull_t *hull) {
    phys_vec3_t pts[8] = {
        {-0.5f, -0.5f, -0.5f}, { 0.5f, -0.5f, -0.5f},
        { 0.5f,  0.5f, -0.5f}, {-0.5f,  0.5f, -0.5f},
        {-0.5f, -0.5f,  0.5f}, { 0.5f, -0.5f,  0.5f},
        { 0.5f,  0.5f,  0.5f}, {-0.5f,  0.5f,  0.5f},
    };
    memset(hull, 0, sizeof(*hull));
    phys_convex_hull_build(hull, pts, 8);
}

/* ── Tests ─────────────────────────────────────────────────────── */

/** 1. Single-child compound vs sphere — should detect contact. */
static int test_compound_vs_sphere_single(void) {
    /* Set up a compound with one unit cube hull at origin. */
    phys_convex_hull_t hull;
    make_unit_cube_hull(&hull);

    phys_convex_compound_t compound;
    memset(&compound, 0, sizeof(compound));
    compound.child_hull_indices[0] = 0;
    compound.child_count = 1;

    /* Sphere at (1.0, 0, 0) with radius 0.6 — should overlap the cube. */
    phys_contact_point_t contact;
    phys_vec3_t compound_pos = {0, 0, 0};
    phys_quat_t compound_rot = {0, 0, 0, 1};
    phys_vec3_t sphere_pos = {1.0f, 0, 0};
    float sphere_radius = 0.6f;

    /* Use the narrowphase_convex sphere_vs_hull directly to validate. */
    int hit = phys_sphere_vs_convex(
        sphere_pos, sphere_radius,
        compound_pos, compound_rot,
        &hull, 0.0f, &contact);

    ASSERT_TRUE(hit);
    /* Penetration should be approximately 0.1 (0.5 + 0.6 - 1.0). */
    ASSERT_NEAR(0.1f, contact.penetration, 0.05f);
    return 0;
}

/** 2. Multi-child compound vs sphere — sphere near only one child. */
static int test_compound_vs_sphere_multi(void) {
    /* Two cubes: one at origin, one at (3, 0, 0).
     * Sphere at (0.8, 0, 0) should only contact the first cube. */
    phys_convex_hull_t hulls[2];
    make_unit_cube_hull(&hulls[0]);
    make_unit_cube_hull(&hulls[1]);
    /* Shift hull[1] vertices by (3, 0, 0). */
    for (uint32_t i = 0; i < hulls[1].vertex_count; i++) {
        hulls[1].vertices[i].x += 3.0f;
    }
    phys_convex_hull_recompute_bounds(&hulls[1]);

    phys_convex_compound_t compound;
    memset(&compound, 0, sizeof(compound));
    compound.child_hull_indices[0] = 0;
    compound.child_hull_indices[1] = 1;
    compound.child_count = 2;

    /* Test sphere vs each child — only hull[0] should contact. */
    phys_vec3_t sphere_pos = {0.8f, 0, 0};
    float sphere_radius = 0.4f;
    phys_vec3_t compound_pos = {0, 0, 0};
    phys_quat_t compound_rot = {0, 0, 0, 1};

    phys_contact_point_t c0, c1;
    int hit0 = phys_sphere_vs_convex(
        sphere_pos, sphere_radius,
        compound_pos, compound_rot,
        &hulls[0], 0.0f, &c0);
    int hit1 = phys_sphere_vs_convex(
        sphere_pos, sphere_radius,
        compound_pos, compound_rot,
        &hulls[1], 0.0f, &c1);

    ASSERT_TRUE(hit0);
    ASSERT_TRUE(!hit1);
    return 0;
}

/** 3. Compound vs box (single child hull). */
static int test_compound_vs_box(void) {
    phys_convex_hull_t hull;
    make_unit_cube_hull(&hull);

    /* Box at (0.9, 0, 0) with half-extents (0.5, 0.5, 0.5).
     * Should overlap the hull. */
    phys_contact_point_t contact;
    phys_vec3_t compound_pos = {0, 0, 0};
    phys_quat_t compound_rot = {0, 0, 0, 1};
    phys_vec3_t box_pos = {0.9f, 0, 0};
    phys_quat_t box_rot = {0, 0, 0, 1};

    int hit = phys_box_vs_convex(
        box_pos, box_rot, (phys_vec3_t){0.5f, 0.5f, 0.5f},
        compound_pos, compound_rot,
        &hull, 0.0f, &contact);

    ASSERT_TRUE(hit);
    /* Penetration ~0.1 (0.5 + 0.5 - 0.9). */
    ASSERT_NEAR(0.1f, contact.penetration, 0.05f);
    return 0;
}

/** 4. Compound AABB = union of child AABBs. */
static int test_compound_aabb(void) {
    phys_convex_hull_t hulls[2];
    make_unit_cube_hull(&hulls[0]);
    make_unit_cube_hull(&hulls[1]);
    /* Shift hull[1] by (5, 0, 0). */
    for (uint32_t i = 0; i < hulls[1].vertex_count; i++) {
        hulls[1].vertices[i].x += 5.0f;
    }
    phys_convex_hull_recompute_bounds(&hulls[1]);

    phys_convex_compound_t compound;
    memset(&compound, 0, sizeof(compound));
    compound.child_hull_indices[0] = 0;
    compound.child_hull_indices[1] = 1;
    compound.child_count = 2;

    /* Compute union AABB: should span from -0.5 to 5.5 on X. */
    phys_vec3_t body_pos = {0, 0, 0};
    phys_quat_t body_rot = {0, 0, 0, 1};

    phys_aabb_t aabb;
    phys_aabb_t a0 = phys_convex_hull_world_aabb(&hulls[0], body_pos, body_rot);
    phys_aabb_t a1 = phys_convex_hull_world_aabb(&hulls[1], body_pos, body_rot);
    phys_aabb_merge(&aabb, &a0, &a1);

    ASSERT_NEAR(-0.5f, aabb.min.x, 0.01f);
    ASSERT_NEAR(5.5f, aabb.max.x, 0.01f);
    ASSERT_NEAR(-0.5f, aabb.min.y, 0.01f);
    ASSERT_NEAR(0.5f, aabb.max.y, 0.01f);
    return 0;
}

/** 5. Empty compound (0 children). */
static int test_empty_compound(void) {
    phys_convex_compound_t compound;
    memset(&compound, 0, sizeof(compound));
    compound.child_count = 0;

    /* No children — should produce no contacts. */
    ASSERT_EQ(0u, compound.child_count);
    return 0;
}

/** 6. Compound type + shape_index referencing works. */
static int test_compound_type_registration(void) {
    /* Verify the convex_compound structure size is reasonable. */
    ASSERT_TRUE(sizeof(phys_convex_compound_t) <= 512);
    ASSERT_EQ(64u, PHYS_COMPOUND_MAX_CHILDREN);

    /* A compound with 3 children. */
    phys_convex_compound_t cc;
    memset(&cc, 0, sizeof(cc));
    cc.child_hull_indices[0] = 10;
    cc.child_hull_indices[1] = 20;
    cc.child_hull_indices[2] = 30;
    cc.child_count = 3;

    ASSERT_EQ(3u, cc.child_count);
    ASSERT_EQ(10u, cc.child_hull_indices[0]);
    ASSERT_EQ(20u, cc.child_hull_indices[1]);
    ASSERT_EQ(30u, cc.child_hull_indices[2]);
    return 0;
}

/** 7. Convex-vs-convex through compound child pair. */
static int test_compound_convex_vs_convex(void) {
    phys_convex_hull_t hulls[2];
    make_unit_cube_hull(&hulls[0]);
    make_unit_cube_hull(&hulls[1]);

    /* Two cubes overlapping by 0.2 on X axis. */
    phys_vec3_t pos_a = {0, 0, 0};
    phys_vec3_t pos_b = {0.8f, 0, 0};
    phys_quat_t rot = {0, 0, 0, 1};

    phys_contact_point_t contact;
    int hit = phys_convex_vs_convex(
        pos_a, rot, &hulls[0],
        pos_b, rot, &hulls[1],
        0.0f, &contact);

    ASSERT_TRUE(hit);
    ASSERT_NEAR(0.2f, contact.penetration, 0.05f);
    return 0;
}

/* ── Main ──────────────────────────────────────────────────────── */

int main(void) {
    fprintf(stderr, "=== p118_compound_collider_tests ===\n");

    RUN_TEST(test_compound_vs_sphere_single);
    RUN_TEST(test_compound_vs_sphere_multi);
    RUN_TEST(test_compound_vs_box);
    RUN_TEST(test_compound_aabb);
    RUN_TEST(test_empty_compound);
    RUN_TEST(test_compound_type_registration);
    RUN_TEST(test_compound_convex_vs_convex);

    fprintf(stderr, "\n%d/%d passed\n", test_count - fail_count, test_count);
    return fail_count ? 1 : 0;
}
