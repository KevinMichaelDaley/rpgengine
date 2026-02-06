/**
 * @file p039_physics_narrowphase_tests.c
 * @brief Unit tests for Stage 6: Narrowphase (sphere-sphere).
 *
 * Tests cover: sphere overlap, separated, touching, coincident,
 * narrowphase candidate generation, multiple pairs, and NULL safety.
 */

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/physics/body.h"
#include "ferrum/physics/broadphase.h"
#include "ferrum/physics/collider.h"
#include "ferrum/physics/manifold.h"
#include "ferrum/physics/narrowphase.h"

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
        float _e = (exp), _a = (act), _t = (tol);                              \
        if (fabsf(_e - _a) > _t) {                                             \
            fprintf(stderr, "ASSERT_FLOAT_NEAR failed: %s:%d: "               \
                    "expected %.6f got %.6f (tol %.6f)\n",                      \
                    __FILE__, __LINE__, (double)_e, (double)_a, (double)_t);    \
            return 1;                                                           \
        }                                                                       \
    } while (0)

#define ASSERT_VEC3_NEAR(exp, act, tol)                                        \
    do {                                                                        \
        phys_vec3_t _ev = (exp), _av = (act);                                  \
        float _t = (tol);                                                      \
        if (fabsf(_ev.x - _av.x) > _t || fabsf(_ev.y - _av.y) > _t           \
            || fabsf(_ev.z - _av.z) > _t) {                                    \
            fprintf(stderr, "ASSERT_VEC3_NEAR failed: %s:%d: "                \
                    "expected (%.4f,%.4f,%.4f) got (%.4f,%.4f,%.4f)\n",         \
                    __FILE__, __LINE__,                                         \
                    (double)_ev.x, (double)_ev.y, (double)_ev.z,               \
                    (double)_av.x, (double)_av.y, (double)_av.z);              \
            return 1;                                                           \
        }                                                                       \
    } while (0)

#define RUN_TEST(fn)                                                           \
    do {                                                                        \
        printf("  %-50s", #fn);                                                 \
        int _r = fn();                                                          \
        printf("%s\n", _r ? "FAIL" : "PASS");                                   \
        if (_r) fail_count++;                                                   \
        test_count++;                                                           \
    } while (0)

/* ── Helpers ────────────────────────────────────────────────────── */

/** Identity quaternion. */
static const phys_quat_t QUAT_IDENTITY = {0.0f, 0.0f, 0.0f, 1.0f};

/** Initialize a dynamic body at a position with identity orientation. */
static void make_body(phys_body_t *body, float px, float py, float pz)
{
    phys_body_init(body);
    phys_body_set_mass(body, 1.0f);
    body->position = (phys_vec3_t){px, py, pz};
    body->orientation = QUAT_IDENTITY;
}

/* ── sphere_vs_sphere tests ─────────────────────────────────────── */

/** 1. Overlapping spheres: penetration and normal. */
static int test_sphere_overlap(void)
{
    phys_contact_point_t contact;
    memset(&contact, 0, sizeof(contact));

    bool hit = phys_sphere_vs_sphere(
        (phys_vec3_t){0.0f, 0.0f, 0.0f}, 1.0f,
        (phys_vec3_t){1.5f, 0.0f, 0.0f}, 1.0f,
        &contact);

    ASSERT_TRUE(hit);
    ASSERT_FLOAT_NEAR(0.5f, contact.penetration, 0.001f);
    ASSERT_VEC3_NEAR(((phys_vec3_t){1.0f, 0.0f, 0.0f}), contact.normal, 0.001f);
    return 0;
}

/** 2. Separated spheres: no contact. */
static int test_sphere_separated(void)
{
    phys_contact_point_t contact;
    memset(&contact, 0, sizeof(contact));

    bool hit = phys_sphere_vs_sphere(
        (phys_vec3_t){0.0f, 0.0f, 0.0f}, 1.0f,
        (phys_vec3_t){5.0f, 0.0f, 0.0f}, 1.0f,
        &contact);

    ASSERT_TRUE(!hit);
    return 0;
}

/** 3. Touching spheres: contact with ~0 penetration. */
static int test_sphere_touching(void)
{
    phys_contact_point_t contact;
    memset(&contact, 0, sizeof(contact));

    bool hit = phys_sphere_vs_sphere(
        (phys_vec3_t){0.0f, 0.0f, 0.0f}, 1.0f,
        (phys_vec3_t){2.0f, 0.0f, 0.0f}, 1.0f,
        &contact);

    ASSERT_TRUE(hit);
    ASSERT_FLOAT_NEAR(0.0f, contact.penetration, 0.001f);
    return 0;
}

/** 4. Coincident centers: fallback normal (0,1,0), pen = 2. */
static int test_sphere_coincident(void)
{
    phys_contact_point_t contact;
    memset(&contact, 0, sizeof(contact));

    bool hit = phys_sphere_vs_sphere(
        (phys_vec3_t){0.0f, 0.0f, 0.0f}, 1.0f,
        (phys_vec3_t){0.0f, 0.0f, 0.0f}, 1.0f,
        &contact);

    ASSERT_TRUE(hit);
    ASSERT_FLOAT_NEAR(2.0f, contact.penetration, 0.001f);
    ASSERT_VEC3_NEAR(((phys_vec3_t){0.0f, 1.0f, 0.0f}), contact.normal, 0.001f);
    return 0;
}

/* ── stage narrowphase tests ────────────────────────────────────── */

/** 5. Narrowphase generates a candidate for overlapping sphere bodies. */
static int test_narrowphase_generates_candidate(void)
{
    phys_body_t bodies[2];
    make_body(&bodies[0], 0.0f, 0.0f, 0.0f);
    make_body(&bodies[1], 1.5f, 0.0f, 0.0f);

    phys_sphere_t spheres[1] = {{.radius = 1.0f}};

    phys_collider_t colliders[2];
    phys_collider_init_sphere(&colliders[0], 0, (phys_vec3_t){0, 0, 0});
    phys_collider_init_sphere(&colliders[1], 0, (phys_vec3_t){0, 0, 0});

    phys_collision_pair_t pairs[1] = {{.body_a = 0, .body_b = 1}};

    phys_contact_candidate_t candidates[4];
    memset(candidates, 0, sizeof(candidates));
    uint32_t count = 0;

    phys_narrowphase_args_t args = {
        .bodies = bodies,
        .colliders = colliders,
        .spheres = spheres,
        .pairs = pairs,
        .pair_count = 1,
        .candidates_out = candidates,
        .candidate_count_out = &count,
        .max_candidates = 4,
    };

    phys_stage_narrowphase(&args);

    ASSERT_INT_EQ(1, (int)count);
    ASSERT_INT_EQ(0, (int)candidates[0].body_a);
    ASSERT_INT_EQ(1, (int)candidates[0].body_b);
    ASSERT_INT_EQ(1, (int)candidates[0].contact_count);
    ASSERT_FLOAT_NEAR(0.5f, candidates[0].contacts[0].penetration, 0.001f);
    return 0;
}

/** 6. No contact for separated sphere bodies. */
static int test_narrowphase_no_contact_separated(void)
{
    phys_body_t bodies[2];
    make_body(&bodies[0], 0.0f, 0.0f, 0.0f);
    make_body(&bodies[1], 5.0f, 0.0f, 0.0f);

    phys_sphere_t spheres[1] = {{.radius = 1.0f}};

    phys_collider_t colliders[2];
    phys_collider_init_sphere(&colliders[0], 0, (phys_vec3_t){0, 0, 0});
    phys_collider_init_sphere(&colliders[1], 0, (phys_vec3_t){0, 0, 0});

    phys_collision_pair_t pairs[1] = {{.body_a = 0, .body_b = 1}};

    phys_contact_candidate_t candidates[4];
    memset(candidates, 0, sizeof(candidates));
    uint32_t count = 0;

    phys_narrowphase_args_t args = {
        .bodies = bodies,
        .colliders = colliders,
        .spheres = spheres,
        .pairs = pairs,
        .pair_count = 1,
        .candidates_out = candidates,
        .candidate_count_out = &count,
        .max_candidates = 4,
    };

    phys_stage_narrowphase(&args);

    ASSERT_INT_EQ(0, (int)count);
    return 0;
}

/** 7. Three mutually overlapping spheres → 3 candidates. */
static int test_narrowphase_multiple_pairs(void)
{
    /* Place 3 spheres close together so all pairs overlap (r=1 each). */
    phys_body_t bodies[3];
    make_body(&bodies[0], 0.0f, 0.0f, 0.0f);
    make_body(&bodies[1], 1.0f, 0.0f, 0.0f);
    make_body(&bodies[2], 0.5f, 0.8f, 0.0f);

    phys_sphere_t spheres[1] = {{.radius = 1.0f}};

    phys_collider_t colliders[3];
    phys_collider_init_sphere(&colliders[0], 0, (phys_vec3_t){0, 0, 0});
    phys_collider_init_sphere(&colliders[1], 0, (phys_vec3_t){0, 0, 0});
    phys_collider_init_sphere(&colliders[2], 0, (phys_vec3_t){0, 0, 0});

    phys_collision_pair_t pairs[3] = {
        {.body_a = 0, .body_b = 1},
        {.body_a = 0, .body_b = 2},
        {.body_a = 1, .body_b = 2},
    };

    phys_contact_candidate_t candidates[8];
    memset(candidates, 0, sizeof(candidates));
    uint32_t count = 0;

    phys_narrowphase_args_t args = {
        .bodies = bodies,
        .colliders = colliders,
        .spheres = spheres,
        .pairs = pairs,
        .pair_count = 3,
        .candidates_out = candidates,
        .candidate_count_out = &count,
        .max_candidates = 8,
    };

    phys_stage_narrowphase(&args);

    ASSERT_INT_EQ(3, (int)count);
    return 0;
}

/** 8. NULL args doesn't crash. */
static int test_narrowphase_null_safe(void)
{
    /* NULL top-level args. */
    phys_stage_narrowphase(NULL);

    /* Args with NULL fields. */
    phys_narrowphase_args_t args;
    memset(&args, 0, sizeof(args));
    phys_stage_narrowphase(&args);

    /* Also test sphere_vs_sphere with NULL contact. */
    bool hit = phys_sphere_vs_sphere(
        (phys_vec3_t){0, 0, 0}, 1.0f,
        (phys_vec3_t){0, 0, 0}, 1.0f,
        NULL);
    ASSERT_TRUE(!hit);

    return 0;
}

/* ── Main ───────────────────────────────────────────────────────── */

int main(void)
{
    int test_count = 0;
    int fail_count = 0;

    printf("p039_physics_narrowphase_tests\n");

    RUN_TEST(test_sphere_overlap);
    RUN_TEST(test_sphere_separated);
    RUN_TEST(test_sphere_touching);
    RUN_TEST(test_sphere_coincident);
    RUN_TEST(test_narrowphase_generates_candidate);
    RUN_TEST(test_narrowphase_no_contact_separated);
    RUN_TEST(test_narrowphase_multiple_pairs);
    RUN_TEST(test_narrowphase_null_safe);

    printf("\n  %d/%d passed\n", test_count - fail_count, test_count);
    return fail_count ? 1 : 0;
}
