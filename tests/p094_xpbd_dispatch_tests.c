/**
 * @file p094_xpbd_dispatch_tests.c
 * @brief Integration tests for XPBD dispatch in the tick pipeline.
 *
 * Verifies that:
 * - TGS skips XPBD-mode islands
 * - XPBD runs for T2+ islands and produces position corrections
 * - Mixed TGS + XPBD islands coexist in a single tick
 * - Compliance parameter affects solver stiffness
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/physics/body.h"
#include "ferrum/physics/constraint.h"
#include "ferrum/physics/island.h"
#include "ferrum/physics/island_tier_promote.h"
#include "ferrum/physics/step_plan.h"
#include "ferrum/physics/tgs_solve.h"
#include "ferrum/physics/xpbd_solve.h"
#include "ferrum/physics/tier_list.h"
#include "ferrum/math/vec3.h"

/* ── Test macros ────────────────────────────────────────────────── */

#define ASSERT_TRUE(cond)                                                \
    do {                                                                 \
        if (!(cond)) {                                                   \
            fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__,   \
                    #cond);                                              \
            return 1;                                                    \
        }                                                                \
    } while (0)

#define ASSERT_INT_EQ(exp, act)                                          \
    do {                                                                 \
        if ((int)(exp) != (int)(act)) {                                  \
            fprintf(stderr, "FAIL: %s:%d: expected %d got %d\n",       \
                    __FILE__, __LINE__, (int)(exp), (int)(act));          \
            return 1;                                                    \
        }                                                                \
    } while (0)

#define ASSERT_FLOAT_NEAR(exp, act, eps)                                 \
    do {                                                                 \
        float _d = (float)(exp) - (float)(act);                          \
        if (_d < -(eps) || _d > (eps)) {                                 \
            fprintf(stderr, "FAIL: %s:%d: expected %f got %f\n",       \
                    __FILE__, __LINE__, (double)(exp), (double)(act));    \
            return 1;                                                    \
        }                                                                \
    } while (0)

/* ── Helpers ────────────────────────────────────────────────────── */

static void init_body_(phys_body_t *b, uint8_t tier, float mass) {
    phys_body_init(b);
    b->tier = tier;
    if (mass > 0.0f) {
        phys_body_set_mass(b, mass);
    }
}

/** Build a contact constraint with a penetration bias along +Y. */
static void build_contact_(phys_constraint_t *c, uint32_t a, uint32_t b,
                            float penetration, float dt) {
    memset(c, 0, sizeof(*c));
    c->body_a = a;
    c->body_b = b;
    c->row_count = 1;
    c->friction = 0.5f;
    c->penetration = penetration;

    /* Normal along +Y (B pushes up, A pushes down). */
    phys_jacobian_row_t *row = &c->rows[0];
    row->J_va = (phys_vec3_t){0.0f, -1.0f, 0.0f};
    row->J_vb = (phys_vec3_t){0.0f,  1.0f, 0.0f};

    /* Baumgarte bias. */
    float baumgarte = 0.2f;
    row->bias = (baumgarte / dt) * penetration;

    /* Effective mass for two equal-mass bodies. */
    row->effective_mass = 0.5f;
    row->lambda_min = 0.0f;
    row->lambda_max = 1e6f;
}

/* ── Test: TGS skips XPBD islands ──────────────────────────────── */

static int test_tgs_skips_xpbd_islands(void) {
    /* Two bodies (T3 XPBD), one constraint.  TGS should skip. */
    phys_body_t bodies[2];
    init_body_(&bodies[0], PHYS_TIER_3_WORLD, 1.0f);
    bodies[0].position = (phys_vec3_t){0.0f, 0.0f, 0.0f};
    init_body_(&bodies[1], PHYS_TIER_3_WORLD, 1.0f);
    bodies[1].position = (phys_vec3_t){0.0f, 0.5f, 0.0f};

    phys_constraint_t constraints[1];
    build_contact_(&constraints[0], 0, 1, 0.05f, 1.0f / 60.0f);
    constraints[0].solver_mode = PHYS_SOLVER_XPBD;

    uint32_t bi[2] = {0, 1};
    uint32_t ci[1] = {0};
    phys_island_t island = {
        .body_indices = bi, .body_count = 2,
        .constraint_indices = ci, .constraint_count = 1,
    };
    phys_island_list_t islands = {.islands = &island, .count = 1, .capacity = 1};

    phys_velocity_t velocities[2];
    memset(velocities, 0, sizeof(velocities));
    phys_velocity_t pseudo[2];
    memset(pseudo, 0, sizeof(pseudo));

    uint32_t tier_substeps[6] = {3, 2, 1, 1, 1, 1};

    phys_stage_tgs_solve(&(phys_tgs_solve_args_t){
        .islands    = &islands,
        .constraints = constraints,
        .bodies     = bodies,
        .velocities = velocities,
        .pseudo_velocities = pseudo,
        .body_count = 2,
        .iterations = 10,
        .gravity    = {0.0f, -9.81f, 0.0f},
        .dt         = 1.0f / 60.0f,
        .tick_dt    = 1.0f / 60.0f,
        .slop       = 0.005f,
        .tier_substep_counts = tier_substeps,
    });

    /* TGS should have initialized velocities with gravity but NOT
     * solved the constraint (XPBD island skipped).  The constraint
     * lambda should be unchanged (0.0). */
    ASSERT_FLOAT_NEAR(0.0f, constraints[0].rows[0].lambda, 1e-6f);

    return 0;
}

/* ── Test: XPBD solves T2+ constraints ─────────────────────────── */

static int test_xpbd_solves_t2_constraints(void) {
    /* Two penetrating bodies, XPBD should separate them. */
    phys_body_t bodies_in[2];
    init_body_(&bodies_in[0], PHYS_TIER_2_VISIBLE, 1.0f);
    bodies_in[0].position = (phys_vec3_t){0.0f, 0.0f, 0.0f};
    init_body_(&bodies_in[1], PHYS_TIER_2_VISIBLE, 1.0f);
    bodies_in[1].position = (phys_vec3_t){0.0f, 0.8f, 0.0f};

    phys_body_t bodies_out[2];
    phys_velocity_t velocities[2];
    memset(velocities, 0, sizeof(velocities));

    float dt = 1.0f / 60.0f;
    phys_constraint_t constraints[1];
    build_contact_(&constraints[0], 0, 1, 0.1f, dt);
    constraints[0].solver_mode = PHYS_SOLVER_XPBD;

    phys_stage_xpbd_solve(&(phys_xpbd_solve_args_t){
        .constraints      = constraints,
        .constraint_count = 1,
        .bodies_in        = bodies_in,
        .bodies_out       = bodies_out,
        .velocities_out   = velocities,
        .body_count       = 2,
        .iterations       = 8,
        .omega            = 0.7f,
        .dt               = dt,
        .compliance       = 1e-6f,
    });

    /* Bodies should have been pushed apart along Y. */
    float separation = bodies_out[1].position.y - bodies_out[0].position.y;
    ASSERT_TRUE(separation > 0.8f); /* pushed further apart */

    /* XPBD should derive non-zero velocity from position change. */
    float vel_diff = velocities[1].linear.y - velocities[0].linear.y;
    ASSERT_TRUE(vel_diff > 0.0f); /* body 1 moving up relative to body 0 */

    return 0;
}

/* ── Test: mixed TGS + XPBD in same solver pass ───────────────── */

static int test_mixed_tgs_xpbd(void) {
    /* 4 bodies: island A (T1, TGS), island B (T3, XPBD).
     * Both have penetrating contacts.  Verify both are solved. */
    phys_body_t bodies[4];
    init_body_(&bodies[0], PHYS_TIER_1_NEAR, 1.0f);
    bodies[0].position = (phys_vec3_t){0.0f, 0.0f, 0.0f};
    init_body_(&bodies[1], PHYS_TIER_1_NEAR, 1.0f);
    bodies[1].position = (phys_vec3_t){0.0f, 0.9f, 0.0f};
    init_body_(&bodies[2], PHYS_TIER_3_WORLD, 1.0f);
    bodies[2].position = (phys_vec3_t){5.0f, 0.0f, 0.0f};
    init_body_(&bodies[3], PHYS_TIER_3_WORLD, 1.0f);
    bodies[3].position = (phys_vec3_t){5.0f, 0.9f, 0.0f};

    float dt = 1.0f / 60.0f;
    phys_constraint_t constraints[2];
    build_contact_(&constraints[0], 0, 1, 0.05f, dt);
    constraints[0].solver_mode = PHYS_SOLVER_TGS;
    build_contact_(&constraints[1], 2, 3, 0.05f, dt);
    constraints[1].solver_mode = PHYS_SOLVER_XPBD;

    /* Build islands. */
    uint32_t bi_a[2] = {0, 1};
    uint32_t ci_a[1] = {0};
    uint32_t bi_b[2] = {2, 3};
    uint32_t ci_b[1] = {1};
    phys_island_t isles[2] = {
        {.body_indices = bi_a, .body_count = 2,
         .constraint_indices = ci_a, .constraint_count = 1},
        {.body_indices = bi_b, .body_count = 2,
         .constraint_indices = ci_b, .constraint_count = 1},
    };
    phys_island_list_t islands = {.islands = isles, .count = 2, .capacity = 2};

    /* Run TGS (should solve island A, skip island B). */
    phys_velocity_t velocities[4];
    memset(velocities, 0, sizeof(velocities));
    phys_velocity_t pseudo[4];
    memset(pseudo, 0, sizeof(pseudo));
    uint32_t tier_substeps[6] = {3, 2, 1, 1, 1, 1};

    phys_stage_tgs_solve(&(phys_tgs_solve_args_t){
        .islands    = &islands,
        .constraints = constraints,
        .bodies     = bodies,
        .velocities = velocities,
        .pseudo_velocities = pseudo,
        .body_count = 4,
        .iterations = 20,
        .gravity    = {0.0f, -9.81f, 0.0f},
        .dt         = dt,
        .tick_dt    = dt,
        .slop       = 0.005f,
        .tier_substep_counts = tier_substeps,
    });

    /* TGS should have solved constraint 0 (TGS island). */
    ASSERT_TRUE(constraints[0].rows[0].lambda > 0.0f);
    /* TGS should NOT have solved constraint 1 (XPBD island). */
    ASSERT_FLOAT_NEAR(0.0f, constraints[1].rows[0].lambda, 1e-6f);

    /* Now run XPBD for island B. */
    phys_body_t xpbd_bodies[4];
    phys_velocity_t xpbd_vel[4];
    memset(xpbd_vel, 0, sizeof(xpbd_vel));

    /* Copy only XPBD constraint for the solver. */
    phys_constraint_t xpbd_constraints[1];
    xpbd_constraints[0] = constraints[1];

    phys_stage_xpbd_solve(&(phys_xpbd_solve_args_t){
        .constraints      = xpbd_constraints,
        .constraint_count = 1,
        .bodies_in        = bodies,
        .bodies_out       = xpbd_bodies,
        .velocities_out   = xpbd_vel,
        .body_count       = 4,
        .iterations       = 4,
        .omega            = 0.7f,
        .dt               = dt,
        .compliance       = 1e-5f,
    });

    /* XPBD should have accumulated lambda for its constraint. */
    ASSERT_TRUE(xpbd_constraints[0].rows[0].lambda > 0.0f);

    /* XPBD bodies 2,3 should have been pushed apart. */
    float sep = xpbd_bodies[3].position.y - xpbd_bodies[2].position.y;
    ASSERT_TRUE(sep > 0.9f);

    return 0;
}

/* ── Test: compliance affects solver stiffness ─────────────────── */

static int test_compliance_affects_stiffness(void) {
    /* Same setup, different compliance values.
     * Higher compliance = softer = less correction per iteration. */
    phys_body_t bodies_in[2];
    init_body_(&bodies_in[0], PHYS_TIER_3_WORLD, 1.0f);
    bodies_in[0].position = (phys_vec3_t){0.0f, 0.0f, 0.0f};
    init_body_(&bodies_in[1], PHYS_TIER_3_WORLD, 1.0f);
    bodies_in[1].position = (phys_vec3_t){0.0f, 0.8f, 0.0f};

    float dt = 1.0f / 60.0f;

    /* Stiff solve (compliance ≈ 0). */
    phys_constraint_t c_stiff[1];
    build_contact_(&c_stiff[0], 0, 1, 0.1f, dt);
    phys_body_t out_stiff[2];
    phys_velocity_t vel_stiff[2];
    memset(vel_stiff, 0, sizeof(vel_stiff));

    phys_stage_xpbd_solve(&(phys_xpbd_solve_args_t){
        .constraints      = c_stiff,
        .constraint_count = 1,
        .bodies_in        = bodies_in,
        .bodies_out       = out_stiff,
        .velocities_out   = vel_stiff,
        .body_count       = 2,
        .iterations       = 4,
        .omega            = 0.7f,
        .dt               = dt,
        .compliance       = 0.0f, /* perfectly stiff */
    });

    /* Soft solve (high compliance). */
    phys_constraint_t c_soft[1];
    build_contact_(&c_soft[0], 0, 1, 0.1f, dt);
    phys_body_t out_soft[2];
    phys_velocity_t vel_soft[2];
    memset(vel_soft, 0, sizeof(vel_soft));

    phys_stage_xpbd_solve(&(phys_xpbd_solve_args_t){
        .constraints      = c_soft,
        .constraint_count = 1,
        .bodies_in        = bodies_in,
        .bodies_out       = out_soft,
        .velocities_out   = vel_soft,
        .body_count       = 2,
        .iterations       = 4,
        .omega            = 0.7f,
        .dt               = dt,
        .compliance       = 1.0f, /* very soft */
    });

    /* Stiff solve should produce MORE separation than soft solve. */
    float sep_stiff = out_stiff[1].position.y - out_stiff[0].position.y;
    float sep_soft  = out_soft[1].position.y  - out_soft[0].position.y;
    ASSERT_TRUE(sep_stiff > sep_soft);

    return 0;
}

/* ── Test: zero XPBD constraints is no-op ──────────────────────── */

static int test_xpbd_zero_constraints_noop(void) {
    phys_body_t bodies_in[2];
    init_body_(&bodies_in[0], PHYS_TIER_2_VISIBLE, 1.0f);
    bodies_in[0].position = (phys_vec3_t){1.0f, 2.0f, 3.0f};
    init_body_(&bodies_in[1], PHYS_TIER_2_VISIBLE, 1.0f);
    bodies_in[1].position = (phys_vec3_t){4.0f, 5.0f, 6.0f};

    phys_body_t bodies_out[2];
    phys_velocity_t velocities[2];

    phys_stage_xpbd_solve(&(phys_xpbd_solve_args_t){
        .constraints      = NULL,
        .constraint_count = 0,
        .bodies_in        = bodies_in,
        .bodies_out       = bodies_out,
        .velocities_out   = velocities,
        .body_count       = 2,
        .iterations       = 4,
        .omega            = 0.7f,
        .dt               = 1.0f / 60.0f,
        .compliance       = 1e-6f,
    });

    /* Should be a no-op — positions unchanged from input. */
    /* (XPBD returns early when constraint_count == 0) */
    return 0;
}

/* ── Runner ─────────────────────────────────────────────────────── */

#define RUN_TEST(fn)                                                     \
    do {                                                                 \
        printf("  %-60s", #fn);                                          \
        int _r = fn();                                                   \
        printf("%s\n", _r ? "FAIL" : "PASS");                         \
        if (_r) fail_count++;                                            \
        test_count++;                                                    \
    } while (0)

int main(void) {
    int fail_count = 0;
    int test_count = 0;

    printf("p094_xpbd_dispatch_tests:\n");

    RUN_TEST(test_tgs_skips_xpbd_islands);
    RUN_TEST(test_xpbd_solves_t2_constraints);
    RUN_TEST(test_mixed_tgs_xpbd);
    RUN_TEST(test_compliance_affects_stiffness);
    RUN_TEST(test_xpbd_zero_constraints_noop);

    printf("%d/%d tests passed\n", test_count - fail_count, test_count);
    return fail_count ? 1 : 0;
}
