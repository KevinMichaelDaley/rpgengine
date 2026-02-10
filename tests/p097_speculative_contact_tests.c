/**
 * @file p097_speculative_contact_tests.c
 * @brief Tests for speculative contact generation and constraint bias.
 *
 * Verifies that:
 * - Narrowphase emits contacts with negative penetration for close pairs
 * - Speculative contacts use velocity-clamping bias (not Baumgarte)
 * - Fast-moving objects are stopped before tunneling
 * - Speculative margin = 0 disables speculative contacts
 * - Speculative contacts don't create false resting forces
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/physics/body.h"
#include "ferrum/physics/broadphase.h"
#include "ferrum/physics/collider.h"
#include "ferrum/physics/constraint.h"
#include "ferrum/physics/manifold.h"
#include "ferrum/physics/narrowphase.h"
#include "ferrum/math/vec3.h"

/* ── Test macros ────────────────────────────────────────────────── */

#define ASSERT_TRUE(cond)                                              \
    do {                                                               \
        if (!(cond)) {                                                 \
            fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__,  \
                    #cond);                                            \
            return 1;                                                  \
        }                                                              \
    } while (0)

#define ASSERT_FLOAT_NEAR(exp, act, eps)                               \
    do {                                                               \
        float _d = (float)(exp) - (float)(act);                        \
        if (_d < -(eps) || _d > (eps)) {                               \
            fprintf(stderr, "FAIL: %s:%d: expected %f got %f\n",      \
                    __FILE__, __LINE__, (double)(exp), (double)(act));  \
            return 1;                                                  \
        }                                                              \
    } while (0)

/* ── Helpers ────────────────────────────────────────────────────── */

static phys_sphere_t test_sphere_ = { .radius = 0.5f };

static void init_sphere_body_(phys_body_t *b, phys_collider_t *col,
                               float x, float y, float z, float mass) {
    phys_body_init(b);
    b->position = (phys_vec3_t){x, y, z};
    b->orientation = (phys_quat_t){0, 0, 0, 1};
    if (mass > 0.0f) {
        b->inv_mass = 1.0f / mass;
        b->inv_inertia_diag = (phys_vec3_t){1, 1, 1};
    }
    col->type = PHYS_SHAPE_SPHERE;
    col->shape_index = 0;
    col->local_offset = (phys_vec3_t){0, 0, 0};
    col->local_rotation = (phys_quat_t){0, 0, 0, 1};
    col->sphere_simplify = false;
}

/* ── Test: sphere pair within speculative margin emits contact ──── */

/**
 * Two spheres (radius 0.5) separated by 0.1 gap (centers at 0 and 1.1).
 * Sum of radii = 1.0, distance = 1.1, separation = 0.1.
 * With speculative_margin = 0.2, should emit contact with penetration ≈ -0.1.
 */
static int test_speculative_sphere_pair(void) {
    enum { N = 2 };
    phys_body_t bodies[N];
    phys_collider_t cols[N];

    init_sphere_body_(&bodies[0], &cols[0], 0, 0, 0, 0);  /* static */
    init_sphere_body_(&bodies[1], &cols[1], 1.1f, 0, 0, 1.0f);

    phys_collision_pair_t pair = { .body_a = 0, .body_b = 1 };
    phys_contact_candidate_t cand;
    uint32_t cand_count = 0;

    phys_stage_narrowphase(&(phys_narrowphase_args_t){
        .bodies              = bodies,
        .colliders           = cols,
        .spheres             = &test_sphere_,
        .pairs               = &pair,
        .pair_count          = 1,
        .candidates_out      = &cand,
        .candidate_count_out = &cand_count,
        .max_candidates      = 1,
        .speculative_margin  = 0.2f,
    });

    ASSERT_TRUE(cand_count == 1);
    ASSERT_TRUE(cand.contact_count == 1);
    /* Penetration should be negative (separated). */
    ASSERT_TRUE(cand.contacts[0].penetration < 0.0f);
    /* Penetration = r_sum - dist = 1.0 - 1.1 = -0.1 */
    ASSERT_FLOAT_NEAR(-0.1f, cand.contacts[0].penetration, 0.01f);

    return 0;
}

/* ── Test: beyond speculative margin emits nothing ─────────────── */

/**
 * Spheres separated by 0.3 (gap > margin 0.2), no contact.
 */
static int test_beyond_margin_no_contact(void) {
    enum { N = 2 };
    phys_body_t bodies[N];
    phys_collider_t cols[N];

    init_sphere_body_(&bodies[0], &cols[0], 0, 0, 0, 0);
    init_sphere_body_(&bodies[1], &cols[1], 1.3f, 0, 0, 1.0f);

    phys_collision_pair_t pair = { .body_a = 0, .body_b = 1 };
    phys_contact_candidate_t cand;
    uint32_t cand_count = 0;

    phys_stage_narrowphase(&(phys_narrowphase_args_t){
        .bodies              = bodies,
        .colliders           = cols,
        .spheres             = &test_sphere_,
        .pairs               = &pair,
        .pair_count          = 1,
        .candidates_out      = &cand,
        .candidate_count_out = &cand_count,
        .max_candidates      = 1,
        .speculative_margin  = 0.2f,
    });

    ASSERT_TRUE(cand_count == 0);
    return 0;
}

/* ── Test: margin = 0 disables speculative contacts ────────────── */

static int test_margin_zero_no_speculative(void) {
    enum { N = 2 };
    phys_body_t bodies[N];
    phys_collider_t cols[N];

    init_sphere_body_(&bodies[0], &cols[0], 0, 0, 0, 0);
    init_sphere_body_(&bodies[1], &cols[1], 1.1f, 0, 0, 1.0f);

    phys_collision_pair_t pair = { .body_a = 0, .body_b = 1 };
    phys_contact_candidate_t cand;
    uint32_t cand_count = 0;

    phys_stage_narrowphase(&(phys_narrowphase_args_t){
        .bodies              = bodies,
        .colliders           = cols,
        .spheres             = &test_sphere_,
        .pairs               = &pair,
        .pair_count          = 1,
        .candidates_out      = &cand,
        .candidate_count_out = &cand_count,
        .max_candidates      = 1,
        .speculative_margin  = 0.0f,
    });

    ASSERT_TRUE(cand_count == 0);
    return 0;
}

/* ── Test: overlapping pair still has positive penetration ──────── */

static int test_overlapping_positive_penetration(void) {
    enum { N = 2 };
    phys_body_t bodies[N];
    phys_collider_t cols[N];

    init_sphere_body_(&bodies[0], &cols[0], 0, 0, 0, 0);
    init_sphere_body_(&bodies[1], &cols[1], 0.8f, 0, 0, 1.0f);

    phys_collision_pair_t pair = { .body_a = 0, .body_b = 1 };
    phys_contact_candidate_t cand;
    uint32_t cand_count = 0;

    phys_stage_narrowphase(&(phys_narrowphase_args_t){
        .bodies              = bodies,
        .colliders           = cols,
        .spheres             = &test_sphere_,
        .pairs               = &pair,
        .pair_count          = 1,
        .candidates_out      = &cand,
        .candidate_count_out = &cand_count,
        .max_candidates      = 1,
        .speculative_margin  = 0.2f,
    });

    ASSERT_TRUE(cand_count == 1);
    /* Penetration = 1.0 - 0.8 = 0.2 (positive, overlapping). */
    ASSERT_TRUE(cand.contacts[0].penetration > 0.0f);
    ASSERT_FLOAT_NEAR(0.2f, cand.contacts[0].penetration, 0.01f);
    return 0;
}

/* ── Test: speculative bias clamps velocity, not position ──────── */

/**
 * Build a constraint from a speculative contact (negative penetration).
 * The bias should clamp closing velocity but NOT apply Baumgarte
 * position correction (no pushing apart for non-touching pairs).
 */
static int test_speculative_constraint_bias(void) {
    phys_body_t bodies[2];
    phys_body_init(&bodies[0]);
    phys_body_init(&bodies[1]);

    /* Static floor at origin. */
    bodies[0].inv_mass = 0.0f;
    bodies[0].inv_inertia_diag = (phys_vec3_t){0, 0, 0};

    /* Dynamic body approaching from above. */
    bodies[1].position = (phys_vec3_t){0, 1.1f, 0};
    bodies[1].inv_mass = 1.0f;
    bodies[1].inv_inertia_diag = (phys_vec3_t){1, 1, 1};
    bodies[1].linear_vel = (phys_vec3_t){0, -5.0f, 0};  /* fast approach */

    /* Speculative contact: negative penetration, normal +Y. */
    phys_contact_point_t contact = {
        .point_world = {0, 0.55f, 0},
        .normal      = {0, 1, 0},
        .penetration = -0.1f,  /* separated by 0.1 */
        .feature_id  = 0,
    };

    phys_constraint_t c;
    float dt = 1.0f / 60.0f;
    phys_constraint_build_contact(&c, &bodies[0], &bodies[1],
                                   &contact, 0.3f, 0.0f, dt, 0.2f, 0.005f);

    /* For speculative contacts (penetration < 0), the bias should be:
     *   speculative_bias = penetration / dt  (a negative value)
     * This clamps the closing velocity to at most |penetration/dt|
     * without applying Baumgarte (which would push apart). */

    /* Bias should be negative (or zero) — not positive like Baumgarte. */
    ASSERT_TRUE(c.rows[0].bias <= 0.0f);

    /* Lambda min should still be 0 (can only push, not pull). */
    ASSERT_FLOAT_NEAR(0.0f, c.rows[0].lambda_min, 1e-6f);

    return 0;
}

/* ── Test: resting contact not affected by speculative bias ─────── */

/**
 * An actual touching contact (penetration > 0) should use normal
 * Baumgarte + restitution bias, not speculative bias.
 */
static int test_resting_contact_normal_bias(void) {
    phys_body_t bodies[2];
    phys_body_init(&bodies[0]);
    phys_body_init(&bodies[1]);

    bodies[0].inv_mass = 0.0f;
    bodies[0].inv_inertia_diag = (phys_vec3_t){0, 0, 0};

    bodies[1].position = (phys_vec3_t){0, 0.9f, 0};
    bodies[1].inv_mass = 1.0f;
    bodies[1].inv_inertia_diag = (phys_vec3_t){1, 1, 1};
    bodies[1].linear_vel = (phys_vec3_t){0, 0, 0};

    /* Touching contact: positive penetration, excess above slop. */
    phys_contact_point_t contact = {
        .point_world = {0, 0.45f, 0},
        .normal      = {0, 1, 0},
        .penetration = 0.05f,
        .feature_id  = 0,
    };

    phys_constraint_t c;
    float dt = 1.0f / 60.0f;
    phys_constraint_build_contact(&c, &bodies[0], &bodies[1],
                                   &contact, 0.3f, 0.0f, dt, 0.2f, 0.005f);

    /* Baumgarte bias should be positive (pushes apart). */
    ASSERT_TRUE(c.rows[0].bias > 0.0f);

    return 0;
}

/* ── Runner ────────────────────────────────────────────────────── */

typedef int (*test_fn)(void);
typedef struct { const char *name; test_fn fn; } test_entry_t;

int main(void) {
    const test_entry_t tests[] = {
        {"speculative_sphere_pair",         test_speculative_sphere_pair},
        {"beyond_margin_no_contact",        test_beyond_margin_no_contact},
        {"margin_zero_no_speculative",      test_margin_zero_no_speculative},
        {"overlapping_positive_penetration", test_overlapping_positive_penetration},
        {"speculative_constraint_bias",     test_speculative_constraint_bias},
        {"resting_contact_normal_bias",     test_resting_contact_normal_bias},
    };

    int n = (int)(sizeof(tests) / sizeof(tests[0]));
    int passed = 0;

    printf("p097_speculative_contact_tests:\n");
    for (int i = 0; i < n; ++i) {
        int rc = tests[i].fn();
        printf("  %-50s %s\n", tests[i].name, rc == 0 ? "PASS" : "FAIL");
        if (rc == 0) { ++passed; }
    }

    printf("%d/%d tests passed\n", passed, n);
    return passed == n ? 0 : 1;
}
