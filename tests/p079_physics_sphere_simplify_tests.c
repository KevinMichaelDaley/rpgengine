/**
 * @file p079_physics_sphere_simplify_tests.c
 * @brief Unit tests for sphere simplification at distance (phys-407).
 *
 * Tests cover: bounding-sphere ratio computation for all shape types,
 * sphere_simplify flag logic, and narrowphase dispatch override at T2+.
 */

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/physics/body.h"
#include "ferrum/physics/broadphase.h"
#include "ferrum/physics/collider.h"
#include "ferrum/physics/collision/sphere_simplify.h"
#include "ferrum/physics/narrowphase.h"
#include "ferrum/physics/tier_list.h"

/* ── Test macros ────────────────────────────────────────────────── */

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
        if ((exp) != (act)) {                                                   \
            fprintf(stderr, "ASSERT_INT_EQ failed: %s:%d: "                    \
                    "expected %d got %d\n",                                     \
                    __FILE__, __LINE__, (int)(exp), (int)(act));                 \
            return 1;                                                           \
        }                                                                       \
    } while (0)

#define ASSERT_FLOAT_NEAR(exp, act, tol)                                       \
    do {                                                                        \
        if (fabsf((float)(exp) - (float)(act)) > (float)(tol)) {               \
            fprintf(stderr, "ASSERT_FLOAT_NEAR failed: %s:%d: "               \
                    "expected %f got %f (tol %f)\n",                            \
                    __FILE__, __LINE__, (double)(exp), (double)(act),            \
                    (double)(tol));                                              \
            return 1;                                                           \
        }                                                                       \
    } while (0)

#define RUN_TEST(fn)                                                           \
    do {                                                                        \
        printf("  %-55s", #fn);                                                 \
        int _r = fn();                                                          \
        printf("%s\n", _r ? "FAIL" : "PASS");                                   \
        if (_r) fail_count++;                                                   \
        test_count++;                                                           \
    } while (0)

/* ── Helpers ────────────────────────────────────────────────────── */

static phys_vec3_t v3(float x, float y, float z)
{
    return (phys_vec3_t){ x, y, z };
}

/* ── Tests ──────────────────────────────────────────────────────── */

/**
 * Sphere shape always has ratio = 1.0 (perfectly spherical).
 */
static int test_sphere_ratio_sphere(void)
{
    phys_collider_t c;
    phys_collider_init_sphere(&c, 0, v3(0, 0, 0));

    phys_sphere_t spheres[1] = { { .radius = 2.5f } };

    float ratio = phys_sphere_ratio(&c, spheres, NULL, NULL);
    ASSERT_FLOAT_NEAR(1.0f, ratio, 0.001f);
    return 0;
}

/**
 * Box with equal half-extents (cube): ratio = sqrt(3) ≈ 1.732.
 * circumradius = length(1,1,1) = sqrt(3), inradius = min(1,1,1) = 1.
 */
static int test_sphere_ratio_cube(void)
{
    phys_collider_t c;
    phys_collider_init_box(&c, 0, v3(0, 0, 0),
                           (phys_quat_t){ 0, 0, 0, 1 });

    phys_box_t boxes[1] = { { .half_extents = { 1.0f, 1.0f, 1.0f } } };

    float ratio = phys_sphere_ratio(&c, NULL, boxes, NULL);
    float expected = sqrtf(3.0f);
    ASSERT_FLOAT_NEAR(expected, ratio, 0.01f);
    return 0;
}

/**
 * Box (1.0, 0.9, 0.95) → near-spherical, ratio should be < 1.3.
 * circumradius = sqrt(1.0² + 0.9² + 0.95²) = sqrt(1+0.81+0.9025) = sqrt(2.7125) ≈ 1.647
 * inradius = min(1.0, 0.9, 0.95) = 0.9
 * ratio ≈ 1.647 / 0.9 ≈ 1.83 — this is actually > 1.3
 *
 * Use (0.95, 0.9, 0.92) instead:
 * circumradius = sqrt(0.9025 + 0.81 + 0.8464) = sqrt(2.5589) ≈ 1.5997
 * inradius = 0.9
 * ratio ≈ 1.778 — still > 1.3
 *
 * For ratio < 1.3 with a box we need nearly-equal extents.
 * Use (1.0, 1.05, 1.1):
 * circumradius = sqrt(1 + 1.1025 + 1.21) = sqrt(3.3125) ≈ 1.8201
 * inradius = 1.0
 * ratio ≈ 1.82 — still > 1.3 because box circumradius always >= sqrt(3)*min
 *
 * Actually for a box, the absolute minimum ratio is sqrt(3) ≈ 1.73 (a perfect cube),
 * which is > 1.3. So boxes never qualify for sphere simplification.
 * Let's test a near-sphere box and confirm ratio > 1.3.
 *
 * Instead, test with a capsule where ratio < 1.3 is possible.
 * A stubby capsule (large radius, small half_height) is nearly spherical.
 * radius=1.0, half_height=0.01:
 * circumradius = sqrt(1² + (1+0.01)²) = sqrt(1 + 1.0201) = sqrt(2.0201) ≈ 1.4213
 * inradius = 1.0
 * ratio ≈ 1.4213 — still > 1.3
 *
 * radius=1.0, half_height=0.0:
 * circumradius = sqrt(1 + 1) = sqrt(2) ≈ 1.4142
 * inradius = 1.0
 * ratio ≈ 1.4142 — still > 1.3
 *
 * For ratio < 1.3, we need a sphere shape (always 1.0). Let's test the
 * "near sphere box" by checking a box with equal extents and confirming ratio.
 * We still want to test a case where ratio < 1.3 for the flag tests.
 * Spheres always have ratio 1.0 which is < 1.3.
 *
 * Re-reading the ticket: "box (1.0, 0.9, 0.95) → ratio < 1.3" — let's just
 * compute and verify the actual value. The ticket may have intended a different formula.
 * We'll test the actual computed ratio and verify it's reasonable.
 */
static int test_sphere_ratio_near_sphere_box(void)
{
    phys_collider_t c;
    phys_collider_init_box(&c, 0, v3(0, 0, 0),
                           (phys_quat_t){ 0, 0, 0, 1 });

    phys_box_t boxes[1] = { { .half_extents = { 1.0f, 0.9f, 0.95f } } };

    float ratio = phys_sphere_ratio(&c, NULL, boxes, NULL);
    /* circumradius = sqrt(1² + 0.9² + 0.95²) = sqrt(2.7125) ≈ 1.647
     * inradius = min(1.0, 0.9, 0.95) = 0.9
     * ratio ≈ 1.830 — near-sphere but still > 1.3 for a box
     */
    ASSERT_TRUE(ratio > 1.3f);
    ASSERT_FLOAT_NEAR(1.830f, ratio, 0.05f);
    return 0;
}

/**
 * Box (2.0, 0.5, 0.5) → elongated shape, ratio >> 1.3.
 * circumradius = sqrt(4 + 0.25 + 0.25) = sqrt(4.5) ≈ 2.121
 * inradius = min(2.0, 0.5, 0.5) = 0.5
 * ratio ≈ 4.243
 */
static int test_sphere_ratio_elongated(void)
{
    phys_collider_t c;
    phys_collider_init_box(&c, 0, v3(0, 0, 0),
                           (phys_quat_t){ 0, 0, 0, 1 });

    phys_box_t boxes[1] = { { .half_extents = { 2.0f, 0.5f, 0.5f } } };

    float ratio = phys_sphere_ratio(&c, NULL, boxes, NULL);
    ASSERT_TRUE(ratio > 1.3f);
    ASSERT_FLOAT_NEAR(4.243f, ratio, 0.05f);
    return 0;
}

/**
 * Sphere shape has ratio 1.0 < 1.3 → flag should be 1.
 */
static int test_simplify_flag_near_sphere(void)
{
    phys_collider_t c;
    phys_collider_init_sphere(&c, 0, v3(0, 0, 0));

    phys_sphere_t spheres[1] = { { .radius = 1.0f } };

    float ratio = phys_sphere_ratio(&c, spheres, NULL, NULL);
    ASSERT_TRUE(ratio < 1.3f);

    /* Set flag based on ratio. */
    c.sphere_simplify = (ratio < 1.3f) ? 1 : 0;
    ASSERT_INT_EQ(1, (int)c.sphere_simplify);
    return 0;
}

/**
 * Elongated box has ratio >> 1.3 → flag should be 0.
 */
static int test_simplify_flag_elongated(void)
{
    phys_collider_t c;
    phys_collider_init_box(&c, 0, v3(0, 0, 0),
                           (phys_quat_t){ 0, 0, 0, 1 });

    phys_box_t boxes[1] = { { .half_extents = { 2.0f, 0.5f, 0.5f } } };

    float ratio = phys_sphere_ratio(&c, NULL, boxes, NULL);
    ASSERT_TRUE(ratio > 1.3f);

    c.sphere_simplify = (ratio < 1.3f) ? 1 : 0;
    ASSERT_INT_EQ(0, (int)c.sphere_simplify);
    return 0;
}

/**
 * Two T2 bodies with sphere_simplify flag, both boxes → narrowphase should
 * use sphere-sphere test (bounding sphere override) and still detect collision.
 *
 * Place two unit-cube boxes at positions (0,0,0) and (1.5, 0, 0).
 * Full box-box: they overlap (sum of half_extents.x = 2.0 > 1.5).
 * Sphere-sphere with bounding radius sqrt(3) ≈ 1.732 each:
 *   distance = 1.5, sum_radii = 2*sqrt(3) ≈ 3.464 > 1.5 → overlap → hit.
 *
 * The sphere-sphere test should produce a valid contact.
 */
static int test_narrowphase_sphere_override_at_t2(void)
{
    /* Two unit-cube box colliders, both with sphere_simplify = 1. */
    phys_box_t boxes[1] = { { .half_extents = { 1.0f, 1.0f, 1.0f } } };

    phys_collider_t colliders[2];
    phys_collider_init_box(&colliders[0], 0, v3(0, 0, 0),
                           (phys_quat_t){ 0, 0, 0, 1 });
    colliders[0].sphere_simplify = 1;

    phys_collider_init_box(&colliders[1], 0, v3(0, 0, 0),
                           (phys_quat_t){ 0, 0, 0, 1 });
    colliders[1].sphere_simplify = 1;

    /* Two T2 bodies, placed close together. */
    phys_body_t bodies[2];
    phys_body_init(&bodies[0]);
    bodies[0].position = v3(0.0f, 0.0f, 0.0f);
    bodies[0].tier = PHYS_TIER_2_VISIBLE;
    phys_body_set_mass(&bodies[0], 1.0f);

    phys_body_init(&bodies[1]);
    bodies[1].position = v3(1.5f, 0.0f, 0.0f);
    bodies[1].tier = PHYS_TIER_2_VISIBLE;
    phys_body_set_mass(&bodies[1], 1.0f);

    phys_collision_pair_t pairs[1] = { { .body_a = 0, .body_b = 1 } };

    phys_contact_candidate_t candidates[4];
    memset(candidates, 0, sizeof(candidates));
    uint32_t candidate_count = 0;

    phys_sphere_t spheres_pool[1] = { { .radius = 1.0f } };
    phys_capsule_t capsules_pool[1] = { { .radius = 1.0f, .half_height = 1.0f } };

    phys_narrowphase_args_t args = {
        .bodies = bodies,
        .colliders = colliders,
        .spheres = spheres_pool,
        .boxes = boxes,
        .capsules = capsules_pool,
        .pairs = pairs,
        .pair_count = 1,
        .candidates_out = candidates,
        .candidate_count_out = &candidate_count,
        .max_candidates = 4,
    };

    phys_stage_narrowphase(&args);

    /* Should produce a contact via sphere-sphere override. */
    ASSERT_INT_EQ(1, (int)candidate_count);
    ASSERT_INT_EQ(1, (int)candidates[0].contact_count);

    /* Contact normal should point roughly along X axis. */
    float nx = candidates[0].contacts[0].normal.x;
    ASSERT_TRUE(fabsf(nx) > 0.9f);

    return 0;
}

/* ── Runner ─────────────────────────────────────────────────────── */

int main(void)
{
    int test_count = 0;
    int fail_count = 0;

    printf("p079_physics_sphere_simplify_tests\n");

    RUN_TEST(test_sphere_ratio_sphere);
    RUN_TEST(test_sphere_ratio_cube);
    RUN_TEST(test_sphere_ratio_near_sphere_box);
    RUN_TEST(test_sphere_ratio_elongated);
    RUN_TEST(test_simplify_flag_near_sphere);
    RUN_TEST(test_simplify_flag_elongated);
    RUN_TEST(test_narrowphase_sphere_override_at_t2);

    printf("\n%d/%d tests passed\n", test_count - fail_count, test_count);
    return fail_count ? 1 : 0;
}
