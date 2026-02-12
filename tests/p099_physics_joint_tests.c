/**
 * @file p099_physics_joint_tests.c
 * @brief Unit tests for joint structures and constraint row builders (phys-801).
 *
 * Tests distance, ball, and hinge joints — happy path, edge cases, and
 * failure modes.  Each joint builder must produce Jacobian rows compatible
 * with the existing TGS/XPBD solver.
 */

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include "ferrum/physics/joint.h"
#include "ferrum/physics/constraint.h"
#include "ferrum/physics/body.h"
#include "ferrum/math/vec3.h"
#include "ferrum/math/quat.h"

/* ── Test macros ────────────────────────────────────────────────── */

#define ASSERT_TRUE(cond)                                                    \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n",              \
                    __FILE__, __LINE__, #cond);                              \
            return 1;                                                        \
        }                                                                    \
    } while (0)

#define ASSERT_INT_EQ(exp, act)                                              \
    do {                                                                     \
        if ((exp) != (act)) {                                                \
            fprintf(stderr, "ASSERT_INT_EQ failed: %s:%d: expected %d "     \
                    "got %d\n", __FILE__, __LINE__, (int)(exp), (int)(act)); \
            return 1;                                                        \
        }                                                                    \
    } while (0)

#define ASSERT_FLOAT_NEAR(exp, act, eps)                                     \
    do {                                                                     \
        float _e = (float)(exp);                                             \
        float _a = (float)(act);                                             \
        if (fabsf(_e - _a) > (eps)) {                                        \
            fprintf(stderr, "ASSERT_FLOAT_NEAR failed: %s:%d: expected "    \
                    "%f got %f (eps=%f)\n", __FILE__, __LINE__,              \
                    (double)_e, (double)_a, (double)(eps));                   \
            return 1;                                                        \
        }                                                                    \
    } while (0)

#define ASSERT_VEC3_NEAR(exp, act, eps)                                      \
    do {                                                                     \
        phys_vec3_t _ev = (exp);                                             \
        phys_vec3_t _av = (act);                                             \
        if (fabsf(_ev.x - _av.x) > (eps) ||                                 \
            fabsf(_ev.y - _av.y) > (eps) ||                                  \
            fabsf(_ev.z - _av.z) > (eps)) {                                  \
            fprintf(stderr, "ASSERT_VEC3_NEAR failed: %s:%d: expected "     \
                    "(%f,%f,%f) got (%f,%f,%f) (eps=%f)\n", __FILE__,        \
                    __LINE__, (double)_ev.x, (double)_ev.y, (double)_ev.z,   \
                    (double)_av.x, (double)_av.y, (double)_av.z,             \
                    (double)(eps));                                           \
            return 1;                                                        \
        }                                                                    \
    } while (0)

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static const float EPS = 1e-5f;

/* ── Helpers ────────────────────────────────────────────────────── */

/** Create two 1 kg sphere bodies separated along X. */
static void setup_bodies(phys_body_t *a, phys_body_t *b) {
    phys_body_init(a);
    phys_body_init(b);
    phys_body_set_mass(a, 1.0f);
    phys_body_set_mass(b, 1.0f);
    phys_body_set_sphere_inertia(a, 1.0f, 0.5f);
    phys_body_set_sphere_inertia(b, 1.0f, 0.5f);
    a->position = (phys_vec3_t){-1.0f, 0.0f, 0.0f};
    b->position = (phys_vec3_t){ 1.0f, 0.0f, 0.0f};
}

/** Check that a Jacobian row has unit-length linear Jacobians. */
static bool row_jacobians_valid(const phys_jacobian_row_t *r) {
    float len_va = sqrtf(r->J_va.x * r->J_va.x +
                         r->J_va.y * r->J_va.y +
                         r->J_va.z * r->J_va.z);
    float len_vb = sqrtf(r->J_vb.x * r->J_vb.x +
                         r->J_vb.y * r->J_vb.y +
                         r->J_vb.z * r->J_vb.z);
    /* Linear Jacobians should be unit length (or zero for pure angular). */
    if (len_va > 1e-6f && fabsf(len_va - 1.0f) > 0.01f) return false;
    if (len_vb > 1e-6f && fabsf(len_vb - 1.0f) > 0.01f) return false;
    return true;
}

/* ══════════════════════════════════════════════════════════════════
 * JOINT STRUCT TESTS
 * ══════════════════════════════════════════════════════════════════ */

/** Joint init sets sensible defaults. */
static int test_joint_init_defaults(void) {
    phys_joint_t j;
    phys_joint_init(&j);
    ASSERT_INT_EQ(PHYS_JOINT_DISTANCE, j.type);
    ASSERT_TRUE(j.body_a == UINT32_MAX);
    ASSERT_TRUE(j.body_b == UINT32_MAX);
    ASSERT_INT_EQ(0, j.row_count);
    ASSERT_FLOAT_NEAR(0.0f, j.rest_length, EPS);
    ASSERT_FLOAT_NEAR(0.0f, j.stiffness, EPS);
    ASSERT_FLOAT_NEAR(0.0f, j.damping, EPS);
    return 0;
}

/** Joint init is NULL-safe. */
static int test_joint_init_null(void) {
    phys_joint_init(NULL);  /* must not crash */
    return 0;
}

/* ══════════════════════════════════════════════════════════════════
 * DISTANCE JOINT TESTS
 * ══════════════════════════════════════════════════════════════════ */

/** Distance joint produces exactly 1 row. */
static int test_distance_row_count(void) {
    phys_body_t a, b;
    setup_bodies(&a, &b);

    phys_joint_t j;
    phys_joint_init(&j);
    j.type = PHYS_JOINT_DISTANCE;
    j.body_a = 0;
    j.body_b = 1;
    j.local_anchor_a = (phys_vec3_t){0.5f, 0.0f, 0.0f};
    j.local_anchor_b = (phys_vec3_t){-0.5f, 0.0f, 0.0f};
    j.rest_length = 1.0f;

    phys_joint_build_distance(&j, &a, &b, 1.0f / 60.0f);
    ASSERT_INT_EQ(1, j.row_count);
    return 0;
}

/** Distance joint Jacobians point along the separation axis. */
static int test_distance_jacobian_direction(void) {
    phys_body_t a, b;
    setup_bodies(&a, &b);
    /* Bodies at (-1,0,0) and (1,0,0), anchors at body-local (+0.5,0,0)
     * and (-0.5,0,0) → world anchors at (-0.5,0,0) and (0.5,0,0).
     * Separation axis = normalized(anchor_b - anchor_a) = (+1,0,0). */

    phys_joint_t j;
    phys_joint_init(&j);
    j.type = PHYS_JOINT_DISTANCE;
    j.body_a = 0;
    j.body_b = 1;
    j.local_anchor_a = (phys_vec3_t){0.5f, 0.0f, 0.0f};
    j.local_anchor_b = (phys_vec3_t){-0.5f, 0.0f, 0.0f};
    j.rest_length = 1.0f;

    phys_joint_build_distance(&j, &a, &b, 1.0f / 60.0f);

    /* J_va = -dir = (-1,0,0), J_vb = +dir = (+1,0,0). */
    ASSERT_VEC3_NEAR(((phys_vec3_t){-1.0f, 0.0f, 0.0f}), j.rows[0].J_va, EPS);
    ASSERT_VEC3_NEAR(((phys_vec3_t){ 1.0f, 0.0f, 0.0f}), j.rows[0].J_vb, EPS);
    return 0;
}

/** Distance joint effective mass is positive. */
static int test_distance_effective_mass(void) {
    phys_body_t a, b;
    setup_bodies(&a, &b);

    phys_joint_t j;
    phys_joint_init(&j);
    j.type = PHYS_JOINT_DISTANCE;
    j.body_a = 0;
    j.body_b = 1;
    j.local_anchor_a = (phys_vec3_t){0.5f, 0.0f, 0.0f};
    j.local_anchor_b = (phys_vec3_t){-0.5f, 0.0f, 0.0f};
    j.rest_length = 1.0f;

    phys_joint_build_distance(&j, &a, &b, 1.0f / 60.0f);
    ASSERT_TRUE(j.rows[0].effective_mass > 0.0f);
    return 0;
}

/** Distance joint bias corrects when separation != rest_length. */
static int test_distance_bias_nonzero_error(void) {
    phys_body_t a, b;
    setup_bodies(&a, &b);

    phys_joint_t j;
    phys_joint_init(&j);
    j.type = PHYS_JOINT_DISTANCE;
    j.body_a = 0;
    j.body_b = 1;
    j.local_anchor_a = (phys_vec3_t){0.5f, 0.0f, 0.0f};
    j.local_anchor_b = (phys_vec3_t){-0.5f, 0.0f, 0.0f};
    j.rest_length = 0.5f;  /* actual distance = 1.0, rest = 0.5 → error */

    phys_joint_build_distance(&j, &a, &b, 1.0f / 60.0f);
    /* Bias should be nonzero to correct the error. */
    ASSERT_TRUE(fabsf(j.rows[0].bias) > 1e-6f);
    return 0;
}

/** Distance joint bias is zero when at rest length. */
static int test_distance_bias_at_rest(void) {
    phys_body_t a, b;
    setup_bodies(&a, &b);

    phys_joint_t j;
    phys_joint_init(&j);
    j.type = PHYS_JOINT_DISTANCE;
    j.body_a = 0;
    j.body_b = 1;
    j.local_anchor_a = (phys_vec3_t){0.5f, 0.0f, 0.0f};
    j.local_anchor_b = (phys_vec3_t){-0.5f, 0.0f, 0.0f};
    j.rest_length = 1.0f;  /* matches actual distance */

    phys_joint_build_distance(&j, &a, &b, 1.0f / 60.0f);
    ASSERT_FLOAT_NEAR(0.0f, j.rows[0].bias, EPS);
    return 0;
}

/** Distance joint lambda bounds are bilateral (push and pull). */
static int test_distance_lambda_bilateral(void) {
    phys_body_t a, b;
    setup_bodies(&a, &b);

    phys_joint_t j;
    phys_joint_init(&j);
    j.type = PHYS_JOINT_DISTANCE;
    j.body_a = 0;
    j.body_b = 1;
    j.local_anchor_a = (phys_vec3_t){0.5f, 0.0f, 0.0f};
    j.local_anchor_b = (phys_vec3_t){-0.5f, 0.0f, 0.0f};
    j.rest_length = 1.0f;

    phys_joint_build_distance(&j, &a, &b, 1.0f / 60.0f);
    ASSERT_TRUE(j.rows[0].lambda_min < 0.0f);
    ASSERT_TRUE(j.rows[0].lambda_max > 0.0f);
    return 0;
}

/** Distance joint with one static body still works. */
static int test_distance_one_static(void) {
    phys_body_t a, b;
    setup_bodies(&a, &b);
    a.inv_mass = 0.0f;
    a.inv_inertia_diag = (phys_vec3_t){0.0f, 0.0f, 0.0f};
    a.flags |= PHYS_BODY_FLAG_STATIC;

    phys_joint_t j;
    phys_joint_init(&j);
    j.type = PHYS_JOINT_DISTANCE;
    j.body_a = 0;
    j.body_b = 1;
    j.local_anchor_a = (phys_vec3_t){0.5f, 0.0f, 0.0f};
    j.local_anchor_b = (phys_vec3_t){-0.5f, 0.0f, 0.0f};
    j.rest_length = 1.0f;

    phys_joint_build_distance(&j, &a, &b, 1.0f / 60.0f);
    ASSERT_INT_EQ(1, j.row_count);
    ASSERT_TRUE(j.rows[0].effective_mass > 0.0f);
    return 0;
}

/** Distance joint NULL safety. */
static int test_distance_null_safe(void) {
    phys_body_t a, b;
    setup_bodies(&a, &b);

    phys_joint_t j;
    phys_joint_init(&j);

    phys_joint_build_distance(NULL, &a, &b, 1.0f / 60.0f);
    phys_joint_build_distance(&j, NULL, &b, 1.0f / 60.0f);
    phys_joint_build_distance(&j, &a, NULL, 1.0f / 60.0f);
    /* Zero dt should not crash. */
    phys_joint_build_distance(&j, &a, &b, 0.0f);
    ASSERT_INT_EQ(0, j.row_count);
    return 0;
}

/* ══════════════════════════════════════════════════════════════════
 * BALL JOINT TESTS
 * ══════════════════════════════════════════════════════════════════ */

/** Ball joint produces exactly 3 rows (X, Y, Z positional lock). */
static int test_ball_row_count(void) {
    phys_body_t a, b;
    setup_bodies(&a, &b);

    phys_joint_t j;
    phys_joint_init(&j);
    j.type = PHYS_JOINT_BALL;
    j.body_a = 0;
    j.body_b = 1;
    j.local_anchor_a = (phys_vec3_t){0.5f, 0.0f, 0.0f};
    j.local_anchor_b = (phys_vec3_t){-0.5f, 0.0f, 0.0f};

    phys_joint_build_ball(&j, &a, &b, 1.0f / 60.0f);
    ASSERT_INT_EQ(3, j.row_count);
    return 0;
}

/** Ball joint rows use world-axis directions (X, Y, Z). */
static int test_ball_axes_orthogonal(void) {
    phys_body_t a, b;
    setup_bodies(&a, &b);

    phys_joint_t j;
    phys_joint_init(&j);
    j.type = PHYS_JOINT_BALL;
    j.body_a = 0;
    j.body_b = 1;
    j.local_anchor_a = (phys_vec3_t){0.5f, 0.0f, 0.0f};
    j.local_anchor_b = (phys_vec3_t){-0.5f, 0.0f, 0.0f};

    phys_joint_build_ball(&j, &a, &b, 1.0f / 60.0f);

    /* Row 0 → X axis: J_vb = (1,0,0). */
    ASSERT_VEC3_NEAR(((phys_vec3_t){ 1.0f, 0.0f, 0.0f}), j.rows[0].J_vb, EPS);
    /* Row 1 → Y axis: J_vb = (0,1,0). */
    ASSERT_VEC3_NEAR(((phys_vec3_t){ 0.0f, 1.0f, 0.0f}), j.rows[1].J_vb, EPS);
    /* Row 2 → Z axis: J_vb = (0,0,1). */
    ASSERT_VEC3_NEAR(((phys_vec3_t){ 0.0f, 0.0f, 1.0f}), j.rows[2].J_vb, EPS);
    return 0;
}

/** Ball joint effective masses are all positive. */
static int test_ball_effective_mass(void) {
    phys_body_t a, b;
    setup_bodies(&a, &b);

    phys_joint_t j;
    phys_joint_init(&j);
    j.type = PHYS_JOINT_BALL;
    j.body_a = 0;
    j.body_b = 1;
    j.local_anchor_a = (phys_vec3_t){0.5f, 0.0f, 0.0f};
    j.local_anchor_b = (phys_vec3_t){-0.5f, 0.0f, 0.0f};

    phys_joint_build_ball(&j, &a, &b, 1.0f / 60.0f);
    for (int i = 0; i < 3; ++i) {
        ASSERT_TRUE(j.rows[i].effective_mass > 0.0f);
    }
    return 0;
}

/** Ball joint bias corrects anchor separation. */
static int test_ball_bias_separated(void) {
    phys_body_t a, b;
    setup_bodies(&a, &b);
    /* Anchors at world (-0.5,0,0) and (0.5,0,0) → separated by 1.0 in X. */

    phys_joint_t j;
    phys_joint_init(&j);
    j.type = PHYS_JOINT_BALL;
    j.body_a = 0;
    j.body_b = 1;
    j.local_anchor_a = (phys_vec3_t){0.5f, 0.0f, 0.0f};
    j.local_anchor_b = (phys_vec3_t){-0.5f, 0.0f, 0.0f};

    phys_joint_build_ball(&j, &a, &b, 1.0f / 60.0f);
    /* X row should have nonzero bias (anchor error in X). */
    ASSERT_TRUE(fabsf(j.rows[0].bias) > 1e-6f);
    /* Y, Z rows should have ~zero bias (no error in those axes). */
    ASSERT_FLOAT_NEAR(0.0f, j.rows[1].bias, EPS);
    ASSERT_FLOAT_NEAR(0.0f, j.rows[2].bias, EPS);
    return 0;
}

/** Ball joint with coincident anchors has zero bias. */
static int test_ball_bias_coincident(void) {
    phys_body_t a, b;
    phys_body_init(&a);
    phys_body_init(&b);
    phys_body_set_mass(&a, 1.0f);
    phys_body_set_mass(&b, 1.0f);
    phys_body_set_sphere_inertia(&a, 1.0f, 0.5f);
    phys_body_set_sphere_inertia(&b, 1.0f, 0.5f);
    a.position = (phys_vec3_t){0.0f, 0.0f, 0.0f};
    b.position = (phys_vec3_t){0.0f, 0.0f, 0.0f};

    phys_joint_t j;
    phys_joint_init(&j);
    j.type = PHYS_JOINT_BALL;
    j.body_a = 0;
    j.body_b = 1;
    j.local_anchor_a = (phys_vec3_t){0.0f, 0.0f, 0.0f};
    j.local_anchor_b = (phys_vec3_t){0.0f, 0.0f, 0.0f};

    phys_joint_build_ball(&j, &a, &b, 1.0f / 60.0f);
    for (int i = 0; i < 3; ++i) {
        ASSERT_FLOAT_NEAR(0.0f, j.rows[i].bias, EPS);
    }
    return 0;
}

/** Ball joint rows are bilateral. */
static int test_ball_lambda_bilateral(void) {
    phys_body_t a, b;
    setup_bodies(&a, &b);

    phys_joint_t j;
    phys_joint_init(&j);
    j.type = PHYS_JOINT_BALL;
    j.body_a = 0;
    j.body_b = 1;
    j.local_anchor_a = (phys_vec3_t){0.5f, 0.0f, 0.0f};
    j.local_anchor_b = (phys_vec3_t){-0.5f, 0.0f, 0.0f};

    phys_joint_build_ball(&j, &a, &b, 1.0f / 60.0f);
    for (int i = 0; i < 3; ++i) {
        ASSERT_TRUE(j.rows[i].lambda_min < 0.0f);
        ASSERT_TRUE(j.rows[i].lambda_max > 0.0f);
    }
    return 0;
}

/** Ball joint NULL safety. */
static int test_ball_null_safe(void) {
    phys_body_t a, b;
    setup_bodies(&a, &b);
    phys_joint_t j;
    phys_joint_init(&j);

    phys_joint_build_ball(NULL, &a, &b, 1.0f / 60.0f);
    phys_joint_build_ball(&j, NULL, &b, 1.0f / 60.0f);
    phys_joint_build_ball(&j, &a, NULL, 1.0f / 60.0f);
    phys_joint_build_ball(&j, &a, &b, 0.0f);
    ASSERT_INT_EQ(0, j.row_count);
    return 0;
}

/* ══════════════════════════════════════════════════════════════════
 * HINGE JOINT TESTS
 * ══════════════════════════════════════════════════════════════════ */

/** Hinge joint produces exactly 5 rows (3 positional + 2 angular). */
static int test_hinge_row_count(void) {
    phys_body_t a, b;
    setup_bodies(&a, &b);

    phys_joint_t j;
    phys_joint_init(&j);
    j.type = PHYS_JOINT_HINGE;
    j.body_a = 0;
    j.body_b = 1;
    j.local_anchor_a = (phys_vec3_t){0.5f, 0.0f, 0.0f};
    j.local_anchor_b = (phys_vec3_t){-0.5f, 0.0f, 0.0f};
    j.local_axis_a = (phys_vec3_t){0.0f, 1.0f, 0.0f};

    phys_joint_build_hinge(&j, &a, &b, 1.0f / 60.0f);
    ASSERT_INT_EQ(5, j.row_count);
    return 0;
}

/** Hinge joint first 3 rows are positional (same as ball joint). */
static int test_hinge_positional_rows(void) {
    phys_body_t a, b;
    setup_bodies(&a, &b);

    phys_joint_t j;
    phys_joint_init(&j);
    j.type = PHYS_JOINT_HINGE;
    j.body_a = 0;
    j.body_b = 1;
    j.local_anchor_a = (phys_vec3_t){0.5f, 0.0f, 0.0f};
    j.local_anchor_b = (phys_vec3_t){-0.5f, 0.0f, 0.0f};
    j.local_axis_a = (phys_vec3_t){0.0f, 1.0f, 0.0f};

    phys_joint_build_hinge(&j, &a, &b, 1.0f / 60.0f);

    /* First 3 rows should be positional along X, Y, Z. */
    ASSERT_VEC3_NEAR(((phys_vec3_t){ 1.0f, 0.0f, 0.0f}), j.rows[0].J_vb, EPS);
    ASSERT_VEC3_NEAR(((phys_vec3_t){ 0.0f, 1.0f, 0.0f}), j.rows[1].J_vb, EPS);
    ASSERT_VEC3_NEAR(((phys_vec3_t){ 0.0f, 0.0f, 1.0f}), j.rows[2].J_vb, EPS);
    return 0;
}

/** Hinge joint angular rows have zero linear Jacobians. */
static int test_hinge_angular_rows_zero_linear(void) {
    phys_body_t a, b;
    setup_bodies(&a, &b);

    phys_joint_t j;
    phys_joint_init(&j);
    j.type = PHYS_JOINT_HINGE;
    j.body_a = 0;
    j.body_b = 1;
    j.local_anchor_a = (phys_vec3_t){0.5f, 0.0f, 0.0f};
    j.local_anchor_b = (phys_vec3_t){-0.5f, 0.0f, 0.0f};
    j.local_axis_a = (phys_vec3_t){0.0f, 1.0f, 0.0f};

    phys_joint_build_hinge(&j, &a, &b, 1.0f / 60.0f);

    /* Rows 3 and 4 are pure angular — linear Jacobians should be zero. */
    phys_vec3_t zero = {0.0f, 0.0f, 0.0f};
    ASSERT_VEC3_NEAR(zero, j.rows[3].J_va, EPS);
    ASSERT_VEC3_NEAR(zero, j.rows[3].J_vb, EPS);
    ASSERT_VEC3_NEAR(zero, j.rows[4].J_va, EPS);
    ASSERT_VEC3_NEAR(zero, j.rows[4].J_vb, EPS);
    return 0;
}

/** Hinge joint angular Jacobians are perpendicular to the hinge axis. */
static int test_hinge_angular_perp_to_axis(void) {
    phys_body_t a, b;
    setup_bodies(&a, &b);

    phys_joint_t j;
    phys_joint_init(&j);
    j.type = PHYS_JOINT_HINGE;
    j.body_a = 0;
    j.body_b = 1;
    j.local_anchor_a = (phys_vec3_t){0.5f, 0.0f, 0.0f};
    j.local_anchor_b = (phys_vec3_t){-0.5f, 0.0f, 0.0f};
    j.local_axis_a = (phys_vec3_t){0.0f, 1.0f, 0.0f};

    phys_joint_build_hinge(&j, &a, &b, 1.0f / 60.0f);

    /* With identity orientation, world axis = local axis = (0,1,0).
     * Angular Jacobians (J_wb) should be perpendicular to Y axis. */
    phys_vec3_t axis = {0.0f, 1.0f, 0.0f};
    float dot3 = vec3_dot(j.rows[3].J_wb, axis);
    float dot4 = vec3_dot(j.rows[4].J_wb, axis);
    ASSERT_FLOAT_NEAR(0.0f, dot3, EPS);
    ASSERT_FLOAT_NEAR(0.0f, dot4, EPS);
    return 0;
}

/** Hinge joint angular rows are mutually orthogonal. */
static int test_hinge_angular_mutual_ortho(void) {
    phys_body_t a, b;
    setup_bodies(&a, &b);

    phys_joint_t j;
    phys_joint_init(&j);
    j.type = PHYS_JOINT_HINGE;
    j.body_a = 0;
    j.body_b = 1;
    j.local_anchor_a = (phys_vec3_t){0.5f, 0.0f, 0.0f};
    j.local_anchor_b = (phys_vec3_t){-0.5f, 0.0f, 0.0f};
    j.local_axis_a = (phys_vec3_t){0.0f, 1.0f, 0.0f};

    phys_joint_build_hinge(&j, &a, &b, 1.0f / 60.0f);

    float dot = vec3_dot(j.rows[3].J_wb, j.rows[4].J_wb);
    ASSERT_FLOAT_NEAR(0.0f, dot, EPS);
    return 0;
}

/** Hinge joint effective masses are all positive. */
static int test_hinge_effective_mass(void) {
    phys_body_t a, b;
    setup_bodies(&a, &b);

    phys_joint_t j;
    phys_joint_init(&j);
    j.type = PHYS_JOINT_HINGE;
    j.body_a = 0;
    j.body_b = 1;
    j.local_anchor_a = (phys_vec3_t){0.5f, 0.0f, 0.0f};
    j.local_anchor_b = (phys_vec3_t){-0.5f, 0.0f, 0.0f};
    j.local_axis_a = (phys_vec3_t){0.0f, 1.0f, 0.0f};

    phys_joint_build_hinge(&j, &a, &b, 1.0f / 60.0f);
    for (int i = 0; i < 5; ++i) {
        ASSERT_TRUE(j.rows[i].effective_mass > 0.0f);
    }
    return 0;
}

/** Hinge joint all rows are bilateral. */
static int test_hinge_lambda_bilateral(void) {
    phys_body_t a, b;
    setup_bodies(&a, &b);

    phys_joint_t j;
    phys_joint_init(&j);
    j.type = PHYS_JOINT_HINGE;
    j.body_a = 0;
    j.body_b = 1;
    j.local_anchor_a = (phys_vec3_t){0.5f, 0.0f, 0.0f};
    j.local_anchor_b = (phys_vec3_t){-0.5f, 0.0f, 0.0f};
    j.local_axis_a = (phys_vec3_t){0.0f, 1.0f, 0.0f};

    phys_joint_build_hinge(&j, &a, &b, 1.0f / 60.0f);
    for (int i = 0; i < 5; ++i) {
        ASSERT_TRUE(j.rows[i].lambda_min < 0.0f);
        ASSERT_TRUE(j.rows[i].lambda_max > 0.0f);
    }
    return 0;
}

/** Hinge joint NULL safety. */
static int test_hinge_null_safe(void) {
    phys_body_t a, b;
    setup_bodies(&a, &b);
    phys_joint_t j;
    phys_joint_init(&j);

    phys_joint_build_hinge(NULL, &a, &b, 1.0f / 60.0f);
    phys_joint_build_hinge(&j, NULL, &b, 1.0f / 60.0f);
    phys_joint_build_hinge(&j, &a, NULL, 1.0f / 60.0f);
    phys_joint_build_hinge(&j, &a, &b, 0.0f);
    ASSERT_INT_EQ(0, j.row_count);
    return 0;
}

/* ══════════════════════════════════════════════════════════════════
 * CROSS-JOINT EDGE CASES
 * ══════════════════════════════════════════════════════════════════ */

/** All joint Jacobian rows have valid structure. */
static int test_all_rows_valid_jacobians(void) {
    phys_body_t a, b;
    setup_bodies(&a, &b);

    phys_joint_t j;

    /* Distance */
    phys_joint_init(&j);
    j.type = PHYS_JOINT_DISTANCE;
    j.body_a = 0; j.body_b = 1;
    j.local_anchor_a = (phys_vec3_t){0.5f, 0.0f, 0.0f};
    j.local_anchor_b = (phys_vec3_t){-0.5f, 0.0f, 0.0f};
    j.rest_length = 1.0f;
    phys_joint_build_distance(&j, &a, &b, 1.0f / 60.0f);
    for (int i = 0; i < j.row_count; ++i) {
        ASSERT_TRUE(row_jacobians_valid(&j.rows[i]));
    }

    /* Ball */
    phys_joint_init(&j);
    j.type = PHYS_JOINT_BALL;
    j.body_a = 0; j.body_b = 1;
    j.local_anchor_a = (phys_vec3_t){0.5f, 0.0f, 0.0f};
    j.local_anchor_b = (phys_vec3_t){-0.5f, 0.0f, 0.0f};
    phys_joint_build_ball(&j, &a, &b, 1.0f / 60.0f);
    for (int i = 0; i < j.row_count; ++i) {
        ASSERT_TRUE(row_jacobians_valid(&j.rows[i]));
    }

    /* Hinge (positional rows only; angular rows have zero linear J). */
    phys_joint_init(&j);
    j.type = PHYS_JOINT_HINGE;
    j.body_a = 0; j.body_b = 1;
    j.local_anchor_a = (phys_vec3_t){0.5f, 0.0f, 0.0f};
    j.local_anchor_b = (phys_vec3_t){-0.5f, 0.0f, 0.0f};
    j.local_axis_a = (phys_vec3_t){0.0f, 1.0f, 0.0f};
    phys_joint_build_hinge(&j, &a, &b, 1.0f / 60.0f);
    for (int i = 0; i < 3; ++i) {
        ASSERT_TRUE(row_jacobians_valid(&j.rows[i]));
    }
    return 0;
}

/** Both bodies static → effective mass should be 0. */
static int test_both_static_effective_mass_zero(void) {
    phys_body_t a, b;
    phys_body_init(&a);
    phys_body_init(&b);
    /* Leave both as static (inv_mass = 0). */

    phys_joint_t j;
    phys_joint_init(&j);
    j.type = PHYS_JOINT_BALL;
    j.body_a = 0;
    j.body_b = 1;
    j.local_anchor_a = (phys_vec3_t){0.5f, 0.0f, 0.0f};
    j.local_anchor_b = (phys_vec3_t){-0.5f, 0.0f, 0.0f};

    phys_joint_build_ball(&j, &a, &b, 1.0f / 60.0f);
    for (int i = 0; i < 3; ++i) {
        ASSERT_FLOAT_NEAR(0.0f, j.rows[i].effective_mass, EPS);
    }
    return 0;
}

/** Rotated body: anchor is transformed correctly. */
static int test_rotated_body_anchor(void) {
    phys_body_t a, b;
    setup_bodies(&a, &b);
    /* Rotate body A 90° around Y: local X → world -Z. */
    a.orientation = PHYS_QUAT_FROM_QUAT(
        quat_from_axis_angle((vec3_t){0, 1, 0}, 3.14159265f / 2.0f, 1e-6f));
    a.position = (phys_vec3_t){0.0f, 0.0f, 0.0f};
    b.position = (phys_vec3_t){0.0f, 0.0f, -1.0f};

    phys_joint_t j;
    phys_joint_init(&j);
    j.type = PHYS_JOINT_BALL;
    j.body_a = 0;
    j.body_b = 1;
    /* Body A local (1,0,0) rotated = world (0,0,-1).
     * Body B at (0,0,-1) with anchor (0,0,0) = world (0,0,-1).
     * Anchors coincide → bias should be ~0 on all axes. */
    j.local_anchor_a = (phys_vec3_t){1.0f, 0.0f, 0.0f};
    j.local_anchor_b = (phys_vec3_t){0.0f, 0.0f, 0.0f};

    phys_joint_build_ball(&j, &a, &b, 1.0f / 60.0f);
    ASSERT_INT_EQ(3, j.row_count);
    /* All biases should be near zero. */
    ASSERT_FLOAT_NEAR(0.0f, j.rows[0].bias, 0.1f);
    ASSERT_FLOAT_NEAR(0.0f, j.rows[1].bias, 0.1f);
    ASSERT_FLOAT_NEAR(0.0f, j.rows[2].bias, 0.1f);
    return 0;
}

/* ── Test runner ────────────────────────────────────────────────── */

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    /* Joint struct */
    {"joint_init_defaults",              test_joint_init_defaults},
    {"joint_init_null",                  test_joint_init_null},
    /* Distance */
    {"distance_row_count",              test_distance_row_count},
    {"distance_jacobian_direction",     test_distance_jacobian_direction},
    {"distance_effective_mass",         test_distance_effective_mass},
    {"distance_bias_nonzero_error",     test_distance_bias_nonzero_error},
    {"distance_bias_at_rest",           test_distance_bias_at_rest},
    {"distance_lambda_bilateral",       test_distance_lambda_bilateral},
    {"distance_one_static",             test_distance_one_static},
    {"distance_null_safe",              test_distance_null_safe},
    /* Ball */
    {"ball_row_count",                  test_ball_row_count},
    {"ball_axes_orthogonal",            test_ball_axes_orthogonal},
    {"ball_effective_mass",             test_ball_effective_mass},
    {"ball_bias_separated",             test_ball_bias_separated},
    {"ball_bias_coincident",            test_ball_bias_coincident},
    {"ball_lambda_bilateral",           test_ball_lambda_bilateral},
    {"ball_null_safe",                  test_ball_null_safe},
    /* Hinge */
    {"hinge_row_count",                 test_hinge_row_count},
    {"hinge_positional_rows",           test_hinge_positional_rows},
    {"hinge_angular_rows_zero_linear",  test_hinge_angular_rows_zero_linear},
    {"hinge_angular_perp_to_axis",      test_hinge_angular_perp_to_axis},
    {"hinge_angular_mutual_ortho",      test_hinge_angular_mutual_ortho},
    {"hinge_effective_mass",            test_hinge_effective_mass},
    {"hinge_lambda_bilateral",          test_hinge_lambda_bilateral},
    {"hinge_null_safe",                 test_hinge_null_safe},
    /* Cross-joint */
    {"all_rows_valid_jacobians",        test_all_rows_valid_jacobians},
    {"both_static_effective_mass_zero", test_both_static_effective_mass_zero},
    {"rotated_body_anchor",             test_rotated_body_anchor},
};

int main(void) {
    size_t total = ARRAY_SIZE(TESTS);
    size_t passed = 0;
    for (size_t i = 0; i < total; ++i) {
        struct test_case *tc = &TESTS[i];
        printf("RUN %s\n", tc->name);
        int rc = tc->fn();
        if (rc == 0) {
            printf("OK %s\n", tc->name);
            passed++;
        } else {
            fprintf(stderr, "FAIL %s (rc=%d)\n", tc->name, rc);
            break;
        }
    }
    if (passed == total) {
        printf("All %zu tests passed\n", passed);
        return 0;
    }
    return 1;
}
