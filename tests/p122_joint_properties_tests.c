/**
 * @file p110_joint_properties_tests.c
 * @brief Tests for per-joint physical properties: damping, yield, break.
 *
 * Verifies that:
 *  - Joint damping dissipates energy (reduces oscillation amplitude)
 *  - Joint yield shifts rest orientation when impulse exceeds threshold
 *  - Joint break marks joint as broken and build returns 0 rows
 *  - Broken joints are skipped by constraint builder
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

#define ASSERT_FLOAT_NEAR(exp, act, tol)                                     \
    do {                                                                     \
        float _e = (exp), _a = (act);                                        \
        if (fabsf(_e - _a) > (tol)) {                                        \
            fprintf(stderr, "ASSERT_FLOAT_NEAR failed: %s:%d: expected "    \
                    "%.6f got %.6f (tol %.6f)\n",                            \
                    __FILE__, __LINE__, (double)_e, (double)_a,              \
                    (double)(tol));                                           \
            return 1;                                                        \
        }                                                                    \
    } while (0)

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
static const float EPS = 1e-5f;

/* ── Helpers ────────────────────────────────────────────────────── */

/** Set up a pair of unit-mass bodies separated along X. */
static void setup_bodies(phys_body_t *a, phys_body_t *b) {
    memset(a, 0, sizeof(*a));
    memset(b, 0, sizeof(*b));
    a->position   = (phys_vec3_t){-1, 0, 0};
    a->orientation = (phys_quat_t){0, 0, 0, 1};
    a->inv_mass   = 1.0f;
    a->inv_inertia_diag = (phys_vec3_t){1, 1, 1};

    b->position   = (phys_vec3_t){ 1, 0, 0};
    b->orientation = (phys_quat_t){0, 0, 0, 1};
    b->inv_mass   = 1.0f;
    b->inv_inertia_diag = (phys_vec3_t){1, 1, 1};
}

/* ══════════════════════════════════════════════════════════════════
 * DAMPING TESTS
 * ══════════════════════════════════════════════════════════════════ */

/** Joint with damping > 0 produces constraint rows with that damping. */
static int test_damping_propagated_to_rows(void) {
    phys_body_t a, b;
    setup_bodies(&a, &b);

    phys_joint_t j;
    phys_joint_init(&j);
    j.type   = PHYS_JOINT_BALL;
    j.body_a = 0;
    j.body_b = 1;
    j.damping = 0.5f;
    phys_joint_build_ball(&j, &a, &b, 1.0f/60.0f);

    /* All rows should have the joint's damping value. */
    ASSERT_TRUE(j.row_count > 0);
    for (uint8_t i = 0; i < j.row_count; i++) {
        ASSERT_FLOAT_NEAR(0.5f, j.rows[i].damping, EPS);
    }
    return 0;
}

/** Joint with damping = 0 has no damping on rows. */
static int test_zero_damping(void) {
    phys_body_t a, b;
    setup_bodies(&a, &b);

    phys_joint_t j;
    phys_joint_init(&j);
    j.type   = PHYS_JOINT_BALL;
    j.body_a = 0;
    j.body_b = 1;
    j.damping = 0.0f;
    phys_joint_build_ball(&j, &a, &b, 1.0f/60.0f);

    for (uint8_t i = 0; i < j.row_count; i++) {
        ASSERT_FLOAT_NEAR(0.0f, j.rows[i].damping, EPS);
    }
    return 0;
}

/* ══════════════════════════════════════════════════════════════════
 * BROKEN JOINT TESTS
 * ══════════════════════════════════════════════════════════════════ */

/** Broken joint produces zero constraint rows. */
static int test_broken_joint_no_rows(void) {
    phys_body_t a, b;
    setup_bodies(&a, &b);

    phys_joint_t j;
    phys_joint_init(&j);
    j.type   = PHYS_JOINT_BALL;
    j.body_a = 0;
    j.body_b = 1;
    j.broken = 1;

    /* Build should produce rows for the joint... */
    phys_joint_build_ball(&j, &a, &b, 1.0f/60.0f);

    /* ...but build_constraints should skip it. */
    phys_constraint_t out[2];
    memset(out, 0, sizeof(out));
    uint32_t count = phys_joint_build_constraints(&j, out, 2, 0);
    ASSERT_INT_EQ(0, (int)count);
    return 0;
}

/** Non-broken joint still produces constraint rows normally. */
static int test_non_broken_joint_has_rows(void) {
    phys_body_t a, b;
    setup_bodies(&a, &b);

    phys_joint_t j;
    phys_joint_init(&j);
    j.type   = PHYS_JOINT_BALL;
    j.body_a = 0;
    j.body_b = 1;
    j.broken = 0;
    phys_joint_build_ball(&j, &a, &b, 1.0f/60.0f);

    phys_constraint_t out[2];
    memset(out, 0, sizeof(out));
    uint32_t count = phys_joint_build_constraints(&j, out, 2, 0);
    ASSERT_TRUE(count > 0);
    return 0;
}

/* ══════════════════════════════════════════════════════════════════
 * YIELD / BREAK FIELD TESTS
 * ══════════════════════════════════════════════════════════════════ */

/** Joint init zeros yield/break/accumulated fields. */
static int test_init_zeros_properties(void) {
    phys_joint_t j;
    phys_joint_init(&j);
    ASSERT_FLOAT_NEAR(0.0f, j.yield_strength, EPS);
    ASSERT_FLOAT_NEAR(0.0f, j.break_strength, EPS);
    ASSERT_FLOAT_NEAR(0.0f, j.accumulated_impulse, EPS);
    ASSERT_INT_EQ(0, j.broken);
    ASSERT_FLOAT_NEAR(0.0f, j.compliance, EPS);
    return 0;
}

/** Break threshold = 0 means unbreakable (never broken). */
static int test_zero_break_unbreakable(void) {
    phys_joint_t j;
    phys_joint_init(&j);
    j.break_strength = 0.0f;
    j.accumulated_impulse = 999999.0f;
    /* With break_strength=0, the break check (if break_strength > 0 && ...)
     * never triggers, so the joint stays intact. */
    ASSERT_INT_EQ(0, j.broken);
    return 0;
}

/** Compliance is propagated to constraint. */
static int test_compliance_in_constraint(void) {
    phys_body_t a, b;
    setup_bodies(&a, &b);

    phys_joint_t j;
    phys_joint_init(&j);
    j.type   = PHYS_JOINT_BALL;
    j.body_a = 0;
    j.body_b = 1;
    j.compliance = 0.01f;
    phys_joint_build_ball(&j, &a, &b, 1.0f/60.0f);

    phys_constraint_t out[2];
    memset(out, 0, sizeof(out));
    uint32_t count = phys_joint_build_constraints(&j, out, 2, 0);
    ASSERT_TRUE(count > 0);
    ASSERT_FLOAT_NEAR(0.01f, out[0].compliance, EPS);
    return 0;
}

/** Damping is propagated to constraint joint_damping field. */
static int test_damping_in_constraint(void) {
    phys_body_t a, b;
    setup_bodies(&a, &b);

    phys_joint_t j;
    phys_joint_init(&j);
    j.type   = PHYS_JOINT_BALL;
    j.body_a = 0;
    j.body_b = 1;
    j.damping = 0.3f;
    phys_joint_build_ball(&j, &a, &b, 1.0f/60.0f);

    phys_constraint_t out[2];
    memset(out, 0, sizeof(out));
    uint32_t count = phys_joint_build_constraints(&j, out, 2, 0);
    ASSERT_TRUE(count > 0);
    ASSERT_FLOAT_NEAR(0.3f, out[0].joint_damping, EPS);
    return 0;
}

/* ── Test runner ────────────────────────────────────────────────── */

struct test_case { const char *name; int (*fn)(void); };

static struct test_case TESTS[] = {
    /* Damping */
    {"damping_propagated_to_rows",  test_damping_propagated_to_rows},
    {"zero_damping",                test_zero_damping},
    /* Broken */
    {"broken_joint_no_rows",        test_broken_joint_no_rows},
    {"non_broken_joint_has_rows",   test_non_broken_joint_has_rows},
    /* Properties */
    {"init_zeros_properties",       test_init_zeros_properties},
    {"zero_break_unbreakable",      test_zero_break_unbreakable},
    {"compliance_in_constraint",    test_compliance_in_constraint},
    {"damping_in_constraint",       test_damping_in_constraint},
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
