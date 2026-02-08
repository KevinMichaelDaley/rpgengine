/**
 * @file p083_physics_position_projection_tests.c
 * @brief Tests for sparse per-island position projection (replaces Baumgarte).
 *
 * Covers: Jacobian assembly, Schur complement (A = J M^-1 J^T),
 * dense LDL^T solve, position correction, and velocity sync.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/physics/body.h"
#include "ferrum/physics/constraint.h"
#include "ferrum/physics/island.h"
#include "ferrum/physics/manifold.h"
#include "ferrum/physics/phys_pool.h"
#include "ferrum/physics/position_projection.h"
#include "ferrum/physics/velocity_sync.h"
#include "ferrum/physics/tgs_solve.h"

/* ── Minimal test harness ────────────────────────────────────────── */

static int g_test_count = 0;
static int g_fail_count = 0;

#define ASSERT_TRUE(cond)                                              \
    do {                                                               \
        if (!(cond)) {                                                 \
            printf("ASSERT_TRUE failed: %s:%d: %s\n",                 \
                   __FILE__, __LINE__, #cond);                         \
            return 1;                                                  \
        }                                                              \
    } while (0)

#define ASSERT_FLOAT_NEAR(expected, actual, tol)                       \
    do {                                                               \
        float _e = (expected), _a = (actual), _t = (tol);             \
        if (fabsf(_e - _a) > _t) {                                    \
            printf("ASSERT_FLOAT_NEAR failed: %s:%d: "                \
                   "expected %.6f, got %.6f (tol %.6f)\n",             \
                   __FILE__, __LINE__, (double)_e, (double)_a,         \
                   (double)_t);                                        \
            return 1;                                                  \
        }                                                              \
    } while (0)

#define RUN_TEST(fn)                                                   \
    do {                                                               \
        g_test_count++;                                                \
        printf("  %-56s", #fn);                                        \
        if (fn() == 0) {                                               \
            printf("PASS\n");                                          \
        } else {                                                       \
            printf("FAIL\n");                                          \
            g_fail_count++;                                            \
        }                                                              \
    } while (0)

/* ── Helper: set up a dynamic body with given mass and position ──── */

static void setup_dynamic_body(phys_body_t *b, float mass,
                                float px, float py, float pz,
                                float hx, float hy, float hz) {
    phys_body_init(b);
    phys_body_set_mass(b, mass);
    phys_body_set_box_inertia(b, mass, (phys_vec3_t){hx, hy, hz});
    b->position = (phys_vec3_t){px, py, pz};
}

static void setup_static_body(phys_body_t *b,
                               float px, float py, float pz) {
    phys_body_init(b);
    b->position = (phys_vec3_t){px, py, pz};
}

/* ── Helper: build a single normal-only contact constraint ───────── */

static void build_normal_constraint(phys_constraint_t *c,
                                     uint32_t body_a, uint32_t body_b,
                                     phys_vec3_t normal,
                                     phys_vec3_t contact_pt,
                                     float penetration,
                                     const phys_body_t *bodies) {
    memset(c, 0, sizeof(*c));
    c->body_a     = body_a;
    c->body_b     = body_b;
    c->row_count  = 3;
    c->friction   = 0.5f;

    /* Build normal row Jacobian manually. */
    phys_vec3_t rA = vec3_sub(contact_pt, bodies[body_a].position);
    phys_vec3_t rB = vec3_sub(contact_pt, bodies[body_b].position);

    c->rows[0].J_va = vec3_scale(normal, -1.0f);
    c->rows[0].J_wa = vec3_scale(vec3_cross(rA, normal), -1.0f);
    c->rows[0].J_vb = normal;
    c->rows[0].J_wb = vec3_cross(rB, normal);
    c->rows[0].lambda_min = 0.0f;
    c->rows[0].lambda_max = 1e10f;
    c->rows[0].bias = 0.0f;
    c->penetration = penetration;  /* raw penetration for position projection */
    c->rows[0].effective_mass = phys_compute_effective_mass(
        &c->rows[0],
        bodies[body_a].inv_mass, &bodies[body_a].inv_inertia_diag,
        bodies[body_b].inv_mass, &bodies[body_b].inv_inertia_diag);
}

/* ================================================================== */
/* TEST 1: Two dynamic bodies penetrating — Jacobian + Phi assembly   */
/* ================================================================== */

static int test_jacobian_build_two_body(void) {
    /* Two 1kg unit cubes penetrating along Y axis.
     * Body 0 at (0, 0.4, 0), body 1 at (0, 1.0, 0).
     * Half-extent 0.5 each → surfaces at y=0.9 and y=0.5.
     * Penetration = 0.4, contact at y=0.7, normal = (0,1,0). */
    phys_body_t bodies[2];
    setup_dynamic_body(&bodies[0], 1.0f, 0.0f, 0.4f, 0.0f,
                       0.5f, 0.5f, 0.5f);
    setup_dynamic_body(&bodies[1], 1.0f, 0.0f, 1.0f, 0.0f,
                       0.5f, 0.5f, 0.5f);

    phys_constraint_t constraints[1];
    phys_vec3_t normal = {0.0f, 1.0f, 0.0f};
    phys_vec3_t contact = {0.0f, 0.7f, 0.0f};
    build_normal_constraint(&constraints[0], 0, 1, normal, contact,
                            0.4f, bodies);

    /* Build a single island containing both bodies. */
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 64 * 1024);

    phys_island_t island;
    uint32_t body_idx[2] = {0, 1};
    uint32_t con_idx[1]  = {0};
    island.body_indices       = body_idx;
    island.body_count         = 2;
    island.constraint_indices = con_idx;
    island.constraint_count   = 1;
    island.sleeping           = false;
    island.skip               = false;

    /* Run position projection. */
    phys_position_projection_result_t result;
    memset(&result, 0, sizeof(result));

    phys_position_projection(&(phys_position_projection_args_t){
        .island      = &island,
        .constraints = constraints,
        .bodies      = bodies,
        .body_count  = 2,
        .dt          = 1.0f / 60.0f,
        .slop        = 0.0f,
        .arena       = &arena,
        .result      = &result,
    });

    /* After projection, penetration should be removed.
     * Body 0 should move down, body 1 should move up (equal masses).
     * Total correction should be ~0.4 (penetration), split equally. */
    ASSERT_TRUE(result.success);

    /* Check that position deltas are applied: body 0 down, body 1 up. */
    ASSERT_TRUE(result.position_deltas != NULL);
    float dy0 = result.position_deltas[0].y;  /* should be negative */
    float dy1 = result.position_deltas[1].y;  /* should be positive */
    ASSERT_TRUE(dy0 < 0.0f);
    ASSERT_TRUE(dy1 > 0.0f);

    /* Total correction ~= penetration (0.4). */
    float total_correction = dy1 - dy0;
    ASSERT_FLOAT_NEAR(0.4f, total_correction, 0.05f);

    /* Equal masses → roughly equal and opposite correction. */
    ASSERT_FLOAT_NEAR(-dy0, dy1, 0.05f);

    phys_frame_arena_destroy(&arena);
    return 0;
}

/* ================================================================== */
/* TEST 2: Static-dynamic pair — only dynamic body moves              */
/* ================================================================== */

static int test_static_dynamic_pair(void) {
    /* Static ground at (0, -0.5, 0), dynamic box at (0, 0.3, 0).
     * Penetration 0.2 along Y. Contact at y=0.0. */
    phys_body_t bodies[2];
    setup_static_body(&bodies[0], 0.0f, -0.5f, 0.0f);
    setup_dynamic_body(&bodies[1], 1.0f, 0.0f, 0.3f, 0.0f,
                       0.5f, 0.5f, 0.5f);

    phys_constraint_t constraints[1];
    phys_vec3_t normal = {0.0f, 1.0f, 0.0f};
    phys_vec3_t contact = {0.0f, 0.0f, 0.0f};
    build_normal_constraint(&constraints[0], 0, 1, normal, contact,
                            0.2f, bodies);

    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 64 * 1024);

    phys_island_t island;
    uint32_t body_idx[2] = {0, 1};
    uint32_t con_idx[1]  = {0};
    island.body_indices       = body_idx;
    island.body_count         = 2;
    island.constraint_indices = con_idx;
    island.constraint_count   = 1;
    island.sleeping           = false;
    island.skip               = false;

    phys_position_projection_result_t result;
    memset(&result, 0, sizeof(result));

    phys_position_projection(&(phys_position_projection_args_t){
        .island      = &island,
        .constraints = constraints,
        .bodies      = bodies,
        .body_count  = 2,
        .dt          = 1.0f / 60.0f,
        .slop        = 0.0f,
        .arena       = &arena,
        .result      = &result,
    });

    ASSERT_TRUE(result.success);

    /* Static body should not move. */
    ASSERT_FLOAT_NEAR(0.0f, result.position_deltas[0].x, 1e-6f);
    ASSERT_FLOAT_NEAR(0.0f, result.position_deltas[0].y, 1e-6f);
    ASSERT_FLOAT_NEAR(0.0f, result.position_deltas[0].z, 1e-6f);

    /* Dynamic body should move up by ~0.2 (full penetration). */
    ASSERT_TRUE(result.position_deltas[1].y > 0.0f);
    ASSERT_FLOAT_NEAR(0.2f, result.position_deltas[1].y, 0.05f);

    phys_frame_arena_destroy(&arena);
    return 0;
}

/* ================================================================== */
/* TEST 3: Three-body stack — chained contacts                        */
/* ================================================================== */

static int test_three_body_stack(void) {
    /* Stack of 3 equal 1kg boxes along Y:
     *   body 0 (static ground) at y=-0.5
     *   body 1 at y=0.4  (pen=0.1 with ground, contact y=0.0)
     *   body 2 at y=1.3  (pen=0.1 with body 1, contact y=0.9)
     * Two constraints: (0,1) and (1,2). */
    phys_body_t bodies[3];
    setup_static_body(&bodies[0], 0.0f, -0.5f, 0.0f);
    setup_dynamic_body(&bodies[1], 1.0f, 0.0f, 0.4f, 0.0f,
                       0.5f, 0.5f, 0.5f);
    setup_dynamic_body(&bodies[2], 1.0f, 0.0f, 1.3f, 0.0f,
                       0.5f, 0.5f, 0.5f);

    phys_constraint_t constraints[2];
    phys_vec3_t normal = {0.0f, 1.0f, 0.0f};

    build_normal_constraint(&constraints[0], 0, 1, normal,
                            (phys_vec3_t){0.0f, 0.0f, 0.0f},
                            0.1f, bodies);
    build_normal_constraint(&constraints[1], 1, 2, normal,
                            (phys_vec3_t){0.0f, 0.9f, 0.0f},
                            0.1f, bodies);

    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 64 * 1024);

    phys_island_t island;
    uint32_t body_idx[3] = {0, 1, 2};
    uint32_t con_idx[2]  = {0, 1};
    island.body_indices       = body_idx;
    island.body_count         = 3;
    island.constraint_indices = con_idx;
    island.constraint_count   = 2;
    island.sleeping           = false;
    island.skip               = false;

    phys_position_projection_result_t result;
    memset(&result, 0, sizeof(result));

    phys_position_projection(&(phys_position_projection_args_t){
        .island      = &island,
        .constraints = constraints,
        .bodies      = bodies,
        .body_count  = 3,
        .dt          = 1.0f / 60.0f,
        .slop        = 0.0f,
        .arena       = &arena,
        .result      = &result,
    });

    ASSERT_TRUE(result.success);

    /* Ground should not move. */
    ASSERT_FLOAT_NEAR(0.0f, result.position_deltas[0].y, 1e-6f);

    /* Body 1 should move up (away from ground). */
    ASSERT_TRUE(result.position_deltas[1].y > 0.0f);

    /* Body 2 should move up even more (chain effect). */
    ASSERT_TRUE(result.position_deltas[2].y > 0.0f);

    /* The corrections should resolve the penetrations:
     * After correction, gap between ground and body 1 >= 0,
     * and gap between body 1 and body 2 >= 0. */
    float new_y1 = bodies[1].position.y + result.position_deltas[1].y;
    float new_y2 = bodies[2].position.y + result.position_deltas[2].y;
    /* Body 1 bottom = new_y1 - 0.5, ground top = 0.0 */
    float gap_01 = (new_y1 - 0.5f) - 0.0f;
    ASSERT_TRUE(gap_01 >= -0.01f);  /* Should be ~0 or slightly positive. */
    /* Body 2 bottom = new_y2 - 0.5, body 1 top = new_y1 + 0.5 */
    float gap_12 = (new_y2 - 0.5f) - (new_y1 + 0.5f);
    ASSERT_TRUE(gap_12 >= -0.01f);

    phys_frame_arena_destroy(&arena);
    return 0;
}

/* ================================================================== */
/* TEST 4: No penetration — should be a no-op                         */
/* ================================================================== */

static int test_no_penetration(void) {
    phys_body_t bodies[2];
    setup_dynamic_body(&bodies[0], 1.0f, 0.0f, 0.0f, 0.0f,
                       0.5f, 0.5f, 0.5f);
    setup_dynamic_body(&bodies[1], 1.0f, 0.0f, 2.0f, 0.0f,
                       0.5f, 0.5f, 0.5f);

    phys_constraint_t constraints[1];
    phys_vec3_t normal = {0.0f, 1.0f, 0.0f};
    phys_vec3_t contact = {0.0f, 1.0f, 0.0f};
    /* Zero penetration. */
    build_normal_constraint(&constraints[0], 0, 1, normal, contact,
                            0.0f, bodies);

    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 64 * 1024);

    phys_island_t island;
    uint32_t body_idx[2] = {0, 1};
    uint32_t con_idx[1]  = {0};
    island.body_indices       = body_idx;
    island.body_count         = 2;
    island.constraint_indices = con_idx;
    island.constraint_count   = 1;
    island.sleeping           = false;
    island.skip               = false;

    phys_position_projection_result_t result;
    memset(&result, 0, sizeof(result));

    phys_position_projection(&(phys_position_projection_args_t){
        .island      = &island,
        .constraints = constraints,
        .bodies      = bodies,
        .body_count  = 2,
        .dt          = 1.0f / 60.0f,
        .slop        = 0.0f,
        .arena       = &arena,
        .result      = &result,
    });

    ASSERT_TRUE(result.success);

    /* No penetration → deltas should be essentially zero. */
    ASSERT_FLOAT_NEAR(0.0f, result.position_deltas[0].y, 1e-4f);
    ASSERT_FLOAT_NEAR(0.0f, result.position_deltas[1].y, 1e-4f);

    phys_frame_arena_destroy(&arena);
    return 0;
}

/* ================================================================== */
/* TEST 5: Velocity sync — delta_q / dt                               */
/* ================================================================== */

static int test_velocity_sync(void) {
    /* Same as static-dynamic test but check velocity output. */
    phys_body_t bodies[2];
    setup_static_body(&bodies[0], 0.0f, -0.5f, 0.0f);
    setup_dynamic_body(&bodies[1], 1.0f, 0.0f, 0.3f, 0.0f,
                       0.5f, 0.5f, 0.5f);

    phys_constraint_t constraints[1];
    phys_vec3_t normal = {0.0f, 1.0f, 0.0f};
    phys_vec3_t contact = {0.0f, 0.0f, 0.0f};
    build_normal_constraint(&constraints[0], 0, 1, normal, contact,
                            0.2f, bodies);

    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 64 * 1024);

    phys_island_t island;
    uint32_t body_idx[2] = {0, 1};
    uint32_t con_idx[1]  = {0};
    island.body_indices       = body_idx;
    island.body_count         = 2;
    island.constraint_indices = con_idx;
    island.constraint_count   = 1;
    island.sleeping           = false;
    island.skip               = false;

    float dt = 1.0f / 60.0f;
    phys_position_projection_result_t result;
    memset(&result, 0, sizeof(result));

    phys_position_projection(&(phys_position_projection_args_t){
        .island      = &island,
        .constraints = constraints,
        .bodies      = bodies,
        .body_count  = 2,
        .dt          = dt,
        .slop        = 0.0f,
        .arena       = &arena,
        .result      = &result,
    });

    ASSERT_TRUE(result.success);
    ASSERT_TRUE(result.velocity_deltas != NULL);

    /* Velocity sync: v_delta = position_delta / dt. */
    float expected_vy = result.position_deltas[1].y / dt;
    ASSERT_FLOAT_NEAR(expected_vy, result.velocity_deltas[1].linear.y, 0.1f);

    /* Static body velocity delta should be zero. */
    ASSERT_FLOAT_NEAR(0.0f, result.velocity_deltas[0].linear.y, 1e-6f);

    phys_frame_arena_destroy(&arena);
    return 0;
}

/* ================================================================== */
/* TEST 6: Uneven masses — heavier body moves less                    */
/* ================================================================== */

static int test_uneven_masses(void) {
    /* Body 0: 10kg, body 1: 1kg. Penetration 0.1 along Y.
     * The lighter body should move ~10x more. */
    phys_body_t bodies[2];
    setup_dynamic_body(&bodies[0], 10.0f, 0.0f, 0.0f, 0.0f,
                       0.5f, 0.5f, 0.5f);
    setup_dynamic_body(&bodies[1], 1.0f, 0.0f, 0.9f, 0.0f,
                       0.5f, 0.5f, 0.5f);

    phys_constraint_t constraints[1];
    phys_vec3_t normal = {0.0f, 1.0f, 0.0f};
    phys_vec3_t contact = {0.0f, 0.5f, 0.0f};
    build_normal_constraint(&constraints[0], 0, 1, normal, contact,
                            0.1f, bodies);

    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 64 * 1024);

    phys_island_t island;
    uint32_t body_idx[2] = {0, 1};
    uint32_t con_idx[1]  = {0};
    island.body_indices       = body_idx;
    island.body_count         = 2;
    island.constraint_indices = con_idx;
    island.constraint_count   = 1;
    island.sleeping           = false;
    island.skip               = false;

    phys_position_projection_result_t result;
    memset(&result, 0, sizeof(result));

    phys_position_projection(&(phys_position_projection_args_t){
        .island      = &island,
        .constraints = constraints,
        .bodies      = bodies,
        .body_count  = 2,
        .dt          = 1.0f / 60.0f,
        .slop        = 0.0f,
        .arena       = &arena,
        .result      = &result,
    });

    ASSERT_TRUE(result.success);

    /* Body 0 (heavy) should move down, body 1 (light) should move up. */
    float dy0 = fabsf(result.position_deltas[0].y);
    float dy1 = fabsf(result.position_deltas[1].y);
    ASSERT_TRUE(dy1 > dy0 * 2.0f);  /* Light body moves much more. */

    /* Total correction ≈ penetration. */
    float total = result.position_deltas[1].y - result.position_deltas[0].y;
    ASSERT_FLOAT_NEAR(0.1f, total, 0.02f);

    phys_frame_arena_destroy(&arena);
    return 0;
}

/* ================================================================== */
/* TEST 7: Slop — small penetrations below slop are ignored           */
/* ================================================================== */

static int test_slop_threshold(void) {
    phys_body_t bodies[2];
    setup_static_body(&bodies[0], 0.0f, -0.5f, 0.0f);
    setup_dynamic_body(&bodies[1], 1.0f, 0.0f, 0.48f, 0.0f,
                       0.5f, 0.5f, 0.5f);

    phys_constraint_t constraints[1];
    phys_vec3_t normal = {0.0f, 1.0f, 0.0f};
    phys_vec3_t contact = {0.0f, 0.0f, 0.0f};
    /* Penetration = 0.02, slop = 0.05 → no correction needed. */
    build_normal_constraint(&constraints[0], 0, 1, normal, contact,
                            0.02f, bodies);

    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 64 * 1024);

    phys_island_t island;
    uint32_t body_idx[2] = {0, 1};
    uint32_t con_idx[1]  = {0};
    island.body_indices       = body_idx;
    island.body_count         = 2;
    island.constraint_indices = con_idx;
    island.constraint_count   = 1;
    island.sleeping           = false;
    island.skip               = false;

    phys_position_projection_result_t result;
    memset(&result, 0, sizeof(result));

    phys_position_projection(&(phys_position_projection_args_t){
        .island      = &island,
        .constraints = constraints,
        .bodies      = bodies,
        .body_count  = 2,
        .dt          = 1.0f / 60.0f,
        .slop        = 0.05f,
        .arena       = &arena,
        .result      = &result,
    });

    ASSERT_TRUE(result.success);

    /* Below slop → no correction. */
    ASSERT_FLOAT_NEAR(0.0f, result.position_deltas[1].y, 1e-4f);

    phys_frame_arena_destroy(&arena);
    return 0;
}

/* ================================================================== */
/* TEST 8: NULL args — should not crash                               */
/* ================================================================== */

static int test_null_safety(void) {
    phys_position_projection(NULL);

    phys_position_projection_result_t result;
    memset(&result, 0, sizeof(result));

    phys_position_projection(&(phys_position_projection_args_t){
        .island      = NULL,
        .constraints = NULL,
        .bodies      = NULL,
        .body_count  = 0,
        .dt          = 1.0f / 60.0f,
        .slop        = 0.0f,
        .arena       = NULL,
        .result      = &result,
    });

    /* Should not crash; result.success = false. */
    ASSERT_TRUE(!result.success);
    return 0;
}

/* ================================================================== */
/* TEST 9: Dense LDL^T solver correctness (unit test)                 */
/* ================================================================== */

static int test_ldlt_solve_2x2(void) {
    /* Solve A * x = b where A = [[4, 2], [2, 3]], b = [8, 7].
     * Solution: x = [1, 1] × something... let me compute:
     * 4*1 + 2*1 = 6 ≠ 8. Try x=[1,1]: 4+2=6, 2+3=5.
     * Let me use A = [[2,1],[1,3]], b=[3,4] → x=[1,1] since 2+1=3, 1+3=4.
     */
    float A[4] = {2.0f, 1.0f, 1.0f, 3.0f};
    float b[2] = {3.0f, 4.0f};
    float x[2] = {0.0f, 0.0f};

    bool ok = phys_dense_ldlt_solve(A, b, x, 2);
    ASSERT_TRUE(ok);
    ASSERT_FLOAT_NEAR(1.0f, x[0], 1e-5f);
    ASSERT_FLOAT_NEAR(1.0f, x[1], 1e-5f);

    return 0;
}

/* ================================================================== */
/* TEST 10: Dense LDL^T solver — 3x3 system                          */
/* ================================================================== */

static int test_ldlt_solve_3x3(void) {
    /* A = [[4,2,0],[2,5,1],[0,1,3]], b = [8, 13, 7]
     * Solution: x = [1, 2, ?]...
     * 4*1 + 2*2 + 0 = 8 ✓
     * 2*1 + 5*2 + 1*? = 13 → 12 + ? = 13 → ? = 1 ✓  (but not needed)
     * Actually let me just pick known solution x=[1,1,2]:
     * 4+2+0 = 6, 2+5+2 = 9, 0+1+6 = 7.
     * b = [6, 9, 7]. */
    float A[9] = {4.0f, 2.0f, 0.0f,
                  2.0f, 5.0f, 1.0f,
                  0.0f, 1.0f, 3.0f};
    float b[3] = {6.0f, 9.0f, 7.0f};
    float x[3] = {0.0f, 0.0f, 0.0f};

    bool ok = phys_dense_ldlt_solve(A, b, x, 3);
    ASSERT_TRUE(ok);
    ASSERT_FLOAT_NEAR(1.0f, x[0], 1e-4f);
    ASSERT_FLOAT_NEAR(1.0f, x[1], 1e-4f);
    ASSERT_FLOAT_NEAR(2.0f, x[2], 1e-4f);

    return 0;
}

/* ================================================================== */
/* TEST 11: Dense LDL^T — 1x1 degenerate (zero diagonal)             */
/* ================================================================== */

static int test_ldlt_degenerate(void) {
    /* A = [[0]], b = [5] → should handle gracefully (x = 0). */
    float A[1] = {0.0f};
    float b[1] = {5.0f};
    float x[1] = {99.0f};

    bool ok = phys_dense_ldlt_solve(A, b, x, 1);
    /* May return true with x=0 or return false; either is acceptable. */
    if (ok) {
        ASSERT_FLOAT_NEAR(0.0f, x[0], 1e-4f);
    }
    return 0;
}

/* ================================================================== */
/* TEST 12: Sleeping island — should be a no-op                       */
/* ================================================================== */

static int test_sleeping_island(void) {
    phys_body_t bodies[2];
    setup_static_body(&bodies[0], 0.0f, -0.5f, 0.0f);
    setup_dynamic_body(&bodies[1], 1.0f, 0.0f, 0.3f, 0.0f,
                       0.5f, 0.5f, 0.5f);

    phys_constraint_t constraints[1];
    phys_vec3_t normal = {0.0f, 1.0f, 0.0f};
    phys_vec3_t contact = {0.0f, 0.0f, 0.0f};
    build_normal_constraint(&constraints[0], 0, 1, normal, contact,
                            0.2f, bodies);

    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 64 * 1024);

    phys_island_t island;
    uint32_t body_idx[2] = {0, 1};
    uint32_t con_idx[1]  = {0};
    island.body_indices       = body_idx;
    island.body_count         = 2;
    island.constraint_indices = con_idx;
    island.constraint_count   = 1;
    island.sleeping           = true;  /* sleeping! */
    island.skip               = false;

    phys_position_projection_result_t result;
    memset(&result, 0, sizeof(result));

    phys_position_projection(&(phys_position_projection_args_t){
        .island      = &island,
        .constraints = constraints,
        .bodies      = bodies,
        .body_count  = 2,
        .dt          = 1.0f / 60.0f,
        .slop        = 0.0f,
        .arena       = &arena,
        .result      = &result,
    });

    /* Sleeping island should produce no corrections. */
    ASSERT_TRUE(result.success);
    ASSERT_FLOAT_NEAR(0.0f, result.position_deltas[1].y, 1e-6f);

    phys_frame_arena_destroy(&arena);
    return 0;
}

/* ================================================================== */
/* TEST 13: Velocity sync preserves tangential velocity               */
/* ================================================================== */

static int test_velocity_sync_preserves_tangential(void) {
    /* A dynamic body sitting on a static ground plane with a tangential
     * velocity (sliding along X).  After position projection fixes the
     * penetration, the velocity sync should replace only the Y (normal)
     * component of velocity, leaving the X component untouched. */
    phys_body_t bodies[2];
    setup_static_body(&bodies[0], 0.0f, -0.5f, 0.0f);
    setup_dynamic_body(&bodies[1], 1.0f, 0.0f, 0.3f, 0.0f,
                       0.5f, 0.5f, 0.5f);

    /* Give the dynamic body a tangential sliding velocity. */
    bodies[1].linear_vel = (phys_vec3_t){5.0f, -2.0f, 3.0f};

    phys_constraint_t constraints[1];
    phys_vec3_t normal = {0.0f, 1.0f, 0.0f};
    phys_vec3_t contact = {0.0f, 0.0f, 0.0f};
    build_normal_constraint(&constraints[0], 0, 1, normal, contact,
                            0.2f, bodies);

    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 64 * 1024);

    phys_island_t island;
    uint32_t body_idx[2] = {0, 1};
    uint32_t con_idx[1]  = {0};
    island.body_indices       = body_idx;
    island.body_count         = 2;
    island.constraint_indices = con_idx;
    island.constraint_count   = 1;
    island.sleeping           = false;
    island.skip               = false;

    float dt = 1.0f / 60.0f;
    phys_position_projection_result_t result;
    memset(&result, 0, sizeof(result));

    phys_position_projection(&(phys_position_projection_args_t){
        .island      = &island,
        .constraints = constraints,
        .bodies      = bodies,
        .body_count  = 2,
        .dt          = dt,
        .slop        = 0.0f,
        .arena       = &arena,
        .result      = &result,
    });

    ASSERT_TRUE(result.success);
    ASSERT_TRUE(result.position_deltas != NULL);

    /* Apply velocity sync. */
    phys_velocity_sync_normals(&(phys_velocity_sync_args_t){
        .island          = &island,
        .constraints     = constraints,
        .bodies          = bodies,
        .position_deltas = result.position_deltas,
        .dt              = dt,
    });

    /* X (tangential) velocity should be preserved exactly. */
    ASSERT_FLOAT_NEAR(5.0f, bodies[1].linear_vel.x, 1e-4f);

    /* Z (tangential) velocity should be preserved exactly. */
    ASSERT_FLOAT_NEAR(3.0f, bodies[1].linear_vel.z, 1e-4f);

    /* Y (normal) velocity should be adjusted toward the ERP-scaled
     * correction velocity.  With vel_sync_erp = 0.2, the target is
     * 20% of the full position-correction velocity.  The velocity sync
     * also has to overcome the initial -2.0 approach velocity, so the
     * final value should be non-negative (bodies pushed apart). */
    ASSERT_TRUE(bodies[1].linear_vel.y >= -0.1f);

    /* Static body velocity should remain zero. */
    ASSERT_FLOAT_NEAR(0.0f, bodies[0].linear_vel.x, 1e-6f);
    ASSERT_FLOAT_NEAR(0.0f, bodies[0].linear_vel.y, 1e-6f);
    ASSERT_FLOAT_NEAR(0.0f, bodies[0].linear_vel.z, 1e-6f);

    phys_frame_arena_destroy(&arena);
    return 0;
}

/* ================================================================== */
/* TEST 14: Velocity sync with angled normal preserves tangential     */
/* ================================================================== */

static int test_velocity_sync_angled_normal(void) {
    /* Contact with a 45-degree angled normal.  A body slides along the
     * surface.  Velocity sync should only replace the normal component. */
    phys_body_t bodies[2];
    setup_static_body(&bodies[0], 0.0f, 0.0f, 0.0f);
    setup_dynamic_body(&bodies[1], 1.0f, 0.5f, 0.5f, 0.0f,
                       0.5f, 0.5f, 0.5f);

    /* Angled normal (45 degrees in XY plane). */
    float inv_sqrt2 = 1.0f / sqrtf(2.0f);
    phys_vec3_t normal = {inv_sqrt2, inv_sqrt2, 0.0f};

    /* Give body a velocity along the surface tangent (perpendicular to normal). */
    phys_vec3_t tangent = {-inv_sqrt2, inv_sqrt2, 0.0f};
    bodies[1].linear_vel = vec3_scale(tangent, 4.0f);

    phys_constraint_t constraints[1];
    phys_vec3_t contact = {0.25f, 0.25f, 0.0f};
    build_normal_constraint(&constraints[0], 0, 1, normal, contact,
                            0.15f, bodies);

    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 64 * 1024);

    phys_island_t island;
    uint32_t body_idx[2] = {0, 1};
    uint32_t con_idx[1]  = {0};
    island.body_indices       = body_idx;
    island.body_count         = 2;
    island.constraint_indices = con_idx;
    island.constraint_count   = 1;
    island.sleeping           = false;
    island.skip               = false;

    float dt = 1.0f / 60.0f;

    /* Record original tangential velocity magnitude. */
    float v_tangent_before = vec3_dot(bodies[1].linear_vel, tangent);

    phys_position_projection_result_t result;
    memset(&result, 0, sizeof(result));

    phys_position_projection(&(phys_position_projection_args_t){
        .island      = &island,
        .constraints = constraints,
        .bodies      = bodies,
        .body_count  = 2,
        .dt          = dt,
        .slop        = 0.0f,
        .arena       = &arena,
        .result      = &result,
    });

    ASSERT_TRUE(result.success);

    phys_velocity_sync_normals(&(phys_velocity_sync_args_t){
        .island          = &island,
        .constraints     = constraints,
        .bodies          = bodies,
        .position_deltas = result.position_deltas,
        .dt              = dt,
    });

    /* Tangential component should be preserved. */
    float v_tangent_after = vec3_dot(bodies[1].linear_vel, tangent);
    ASSERT_FLOAT_NEAR(v_tangent_before, v_tangent_after, 1e-4f);

    /* Normal component should match ERP-scaled correction (erp = 0.2). */
    float v_corr_n = vec3_dot(
        vec3_scale(result.position_deltas[1], 0.2f / dt), normal);
    float v_after_n = vec3_dot(bodies[1].linear_vel, normal);
    ASSERT_FLOAT_NEAR(v_corr_n, v_after_n, 0.1f);

    phys_frame_arena_destroy(&arena);
    return 0;
}

/* ================================================================== */
/* TEST 15: Velocity sync with zero correction is no-op               */
/* ================================================================== */

static int test_velocity_sync_no_correction(void) {
    /* No penetration → no position delta → velocity unchanged. */
    phys_body_t bodies[2];
    setup_static_body(&bodies[0], 0.0f, -0.5f, 0.0f);
    setup_dynamic_body(&bodies[1], 1.0f, 0.0f, 1.0f, 0.0f,
                       0.5f, 0.5f, 0.5f);
    bodies[1].linear_vel = (phys_vec3_t){1.0f, -3.0f, 2.0f};

    phys_constraint_t constraints[1];
    phys_vec3_t normal = {0.0f, 1.0f, 0.0f};
    phys_vec3_t contact = {0.0f, 0.0f, 0.0f};
    /* Zero penetration. */
    build_normal_constraint(&constraints[0], 0, 1, normal, contact,
                            0.0f, bodies);

    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 64 * 1024);

    phys_island_t island;
    uint32_t body_idx[2] = {0, 1};
    uint32_t con_idx[1]  = {0};
    island.body_indices       = body_idx;
    island.body_count         = 2;
    island.constraint_indices = con_idx;
    island.constraint_count   = 1;
    island.sleeping           = false;
    island.skip               = false;

    float dt = 1.0f / 60.0f;
    phys_position_projection_result_t result;
    memset(&result, 0, sizeof(result));

    phys_position_projection(&(phys_position_projection_args_t){
        .island      = &island,
        .constraints = constraints,
        .bodies      = bodies,
        .body_count  = 2,
        .dt          = dt,
        .slop        = 0.0f,
        .arena       = &arena,
        .result      = &result,
    });

    ASSERT_TRUE(result.success);

    phys_velocity_sync_normals(&(phys_velocity_sync_args_t){
        .island          = &island,
        .constraints     = constraints,
        .bodies          = bodies,
        .position_deltas = result.position_deltas,
        .dt              = dt,
    });

    /* Velocity should be completely unchanged. */
    ASSERT_FLOAT_NEAR(1.0f, bodies[1].linear_vel.x, 1e-6f);
    ASSERT_FLOAT_NEAR(-3.0f, bodies[1].linear_vel.y, 1e-6f);
    ASSERT_FLOAT_NEAR(2.0f, bodies[1].linear_vel.z, 1e-6f);

    phys_frame_arena_destroy(&arena);
    return 0;
}

/* ── Main ──────────────────────────────────────────────────────────── */

int main(void) {
    printf("p083_physics_position_projection_tests\n");

    RUN_TEST(test_jacobian_build_two_body);
    RUN_TEST(test_static_dynamic_pair);
    RUN_TEST(test_three_body_stack);
    RUN_TEST(test_no_penetration);
    RUN_TEST(test_velocity_sync);
    RUN_TEST(test_uneven_masses);
    RUN_TEST(test_slop_threshold);
    RUN_TEST(test_null_safety);
    RUN_TEST(test_ldlt_solve_2x2);
    RUN_TEST(test_ldlt_solve_3x3);
    RUN_TEST(test_ldlt_degenerate);
    RUN_TEST(test_sleeping_island);
    RUN_TEST(test_velocity_sync_preserves_tangential);
    RUN_TEST(test_velocity_sync_angled_normal);
    RUN_TEST(test_velocity_sync_no_correction);

    printf("\n%d/%d tests passed\n", g_test_count - g_fail_count, g_test_count);
    return g_fail_count > 0 ? 1 : 0;
}
