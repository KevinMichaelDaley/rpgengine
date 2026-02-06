/**
 * @file p046_physics_solver_transition_tests.c
 * @brief Unit tests for solver transition (warm-start conversion).
 *
 * Tests cover: TGS→XPBD conversion, XPBD→TGS conversion, roundtrip
 * fidelity, clamping to prevent energy injection, NULL safety, and
 * zero-dt safety.
 */

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/physics/constraint.h"
#include "ferrum/physics/solver_transition.h"

/* ── Test macros ────────────────────────────────────────────────── */

#define ASSERT_TRUE(cond)                                                      \
    do {                                                                        \
        if (!(cond)) {                                                          \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n",                 \
                    __FILE__, __LINE__, #cond);                                  \
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

#define RUN_TEST(fn)                                                           \
    do {                                                                        \
        printf("  %-50s", #fn);                                                 \
        int _r = fn();                                                          \
        printf("%s\n", _r ? "FAIL" : "PASS");                                   \
        if (_r) fail_count++;                                                   \
        test_count++;                                                           \
    } while (0)

/* ── Helpers ────────────────────────────────────────────────────── */

/** Build a constraint with a single row having the given lambda/clamp values. */
static phys_constraint_t make_single_row(float lambda, float lmin, float lmax)
{
    phys_constraint_t c;
    memset(&c, 0, sizeof(c));
    c.row_count            = 1;
    c.rows[0].lambda       = lambda;
    c.rows[0].lambda_min   = lmin;
    c.rows[0].lambda_max   = lmax;
    return c;
}

/** Build a constraint with three rows (normal + 2 friction). */
static phys_constraint_t make_three_rows(float lambda_n, float lambda_t1,
                                         float lambda_t2)
{
    phys_constraint_t c;
    memset(&c, 0, sizeof(c));
    c.row_count = 3;

    /* Normal row: lambda in [0, +big]. */
    c.rows[0].lambda     = lambda_n;
    c.rows[0].lambda_min = 0.0f;
    c.rows[0].lambda_max = 1e6f;

    /* Tangent rows: symmetric limits. */
    c.rows[1].lambda     = lambda_t1;
    c.rows[1].lambda_min = -1e6f;
    c.rows[1].lambda_max = 1e6f;

    c.rows[2].lambda     = lambda_t2;
    c.rows[2].lambda_min = -1e6f;
    c.rows[2].lambda_max = 1e6f;

    return c;
}

/* ── Tests ──────────────────────────────────────────────────────── */

/**
 * Test 1: TGS→XPBD conversion.
 * lambda_impulse=10, dt=1/60 → lambda_xpbd = 10 * (1/60) ≈ 0.16667
 */
static int test_tgs_to_xpbd_conversion(void)
{
    const float dt = 1.0f / 60.0f;
    phys_constraint_t c = make_single_row(10.0f, 0.0f, 1e6f);

    phys_solver_convert_tgs_to_xpbd(&c, dt);

    ASSERT_FLOAT_NEAR(10.0f * dt, c.rows[0].lambda, 1e-5f);
    return 0;
}

/**
 * Test 2: XPBD→TGS conversion.
 * lambda_xpbd ≈ 0.16667, dt=1/60 → lambda_impulse ≈ 10
 */
static int test_xpbd_to_tgs_conversion(void)
{
    const float dt = 1.0f / 60.0f;
    const float lambda_xpbd = 10.0f * dt;  /* ≈ 0.16667 */
    phys_constraint_t c = make_single_row(lambda_xpbd, 0.0f, 1e6f);

    phys_solver_convert_xpbd_to_tgs(&c, dt);

    ASSERT_FLOAT_NEAR(10.0f, c.rows[0].lambda, 1e-4f);
    return 0;
}

/**
 * Test 3: Roundtrip TGS→XPBD→TGS preserves original lambda.
 * Uses 3 rows to verify all rows are converted.
 */
static int test_roundtrip(void)
{
    const float dt = 1.0f / 60.0f;
    const float orig_n  = 50.0f;
    const float orig_t1 = -3.0f;
    const float orig_t2 = 7.5f;

    phys_constraint_t c = make_three_rows(orig_n, orig_t1, orig_t2);

    phys_solver_convert_tgs_to_xpbd(&c, dt);
    phys_solver_convert_xpbd_to_tgs(&c, dt);

    ASSERT_FLOAT_NEAR(orig_n,  c.rows[0].lambda, 1e-3f);
    ASSERT_FLOAT_NEAR(orig_t1, c.rows[1].lambda, 1e-3f);
    ASSERT_FLOAT_NEAR(orig_t2, c.rows[2].lambda, 1e-3f);
    return 0;
}

/**
 * Test 4: XPBD→TGS clamping prevents energy injection.
 * A large lambda_xpbd that would exceed lambda_max after division
 * must be clamped to lambda_max.
 */
static int test_xpbd_to_tgs_clamping(void)
{
    const float dt = 1.0f / 60.0f;
    /* lambda_xpbd = 1000, divided by dt ≈ 60000, but lambda_max = 100. */
    phys_constraint_t c = make_single_row(1000.0f, 0.0f, 100.0f);

    phys_solver_convert_xpbd_to_tgs(&c, dt);

    ASSERT_FLOAT_NEAR(100.0f, c.rows[0].lambda, 1e-5f);

    /* Also test lambda_min clamping: large negative. */
    phys_constraint_t c2 = make_single_row(-1000.0f, -50.0f, 100.0f);

    phys_solver_convert_xpbd_to_tgs(&c2, dt);

    ASSERT_FLOAT_NEAR(-50.0f, c2.rows[0].lambda, 1e-5f);
    return 0;
}

/**
 * Test 5: NULL constraint doesn't crash.
 */
static int test_null_safe(void)
{
    const float dt = 1.0f / 60.0f;

    /* These must not crash. */
    phys_solver_convert_tgs_to_xpbd(NULL, dt);
    phys_solver_convert_xpbd_to_tgs(NULL, dt);

    ASSERT_TRUE(1);  /* Reached here without crash. */
    return 0;
}

/**
 * Test 6: dt=0 doesn't crash or divide by zero.
 */
static int test_zero_dt_safe(void)
{
    phys_constraint_t c = make_single_row(10.0f, 0.0f, 1e6f);
    float original_lambda = c.rows[0].lambda;

    /* dt=0 should be a no-op (no crash, no change). */
    phys_solver_convert_tgs_to_xpbd(&c, 0.0f);
    ASSERT_FLOAT_NEAR(original_lambda, c.rows[0].lambda, 1e-6f);

    phys_solver_convert_xpbd_to_tgs(&c, 0.0f);
    ASSERT_FLOAT_NEAR(original_lambda, c.rows[0].lambda, 1e-6f);

    /* Negative dt should also be a no-op. */
    phys_solver_convert_tgs_to_xpbd(&c, -1.0f);
    ASSERT_FLOAT_NEAR(original_lambda, c.rows[0].lambda, 1e-6f);

    phys_solver_convert_xpbd_to_tgs(&c, -1.0f);
    ASSERT_FLOAT_NEAR(original_lambda, c.rows[0].lambda, 1e-6f);

    return 0;
}

/* ── Main ───────────────────────────────────────────────────────── */

int main(void)
{
    int test_count = 0;
    int fail_count = 0;

    printf("p046_physics_solver_transition_tests\n");

    RUN_TEST(test_tgs_to_xpbd_conversion);
    RUN_TEST(test_xpbd_to_tgs_conversion);
    RUN_TEST(test_roundtrip);
    RUN_TEST(test_xpbd_to_tgs_clamping);
    RUN_TEST(test_null_safe);
    RUN_TEST(test_zero_dt_safe);

    printf("\n%d/%d tests passed.\n", test_count - fail_count, test_count);
    return fail_count ? 1 : 0;
}
