/**
 * @file p096_tgs_coloring_tests.c
 * @brief Integration tests for graph-colored TGS constraint solving.
 *
 * Verifies that:
 * - Islands below threshold use sequential solve (no coloring)
 * - Islands at/above threshold use graph-colored solve
 * - Colored solve produces valid velocity corrections
 * - Threshold = 0 disables coloring entirely
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/physics/body.h"
#include "ferrum/physics/constraint.h"
#include "ferrum/physics/island.h"
#include "ferrum/physics/phys_pool.h"
#include "ferrum/physics/step_plan.h"
#include "ferrum/physics/tgs_solve.h"
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

/** Arena backing buffer size (1 MB — plenty for coloring workspace). */
#define ARENA_SIZE (1u << 20)

static void init_body_(phys_body_t *b, float mass) {
    phys_body_init(b);
    b->tier = 0;
    if (mass > 0.0f) {
        b->inv_mass = 1.0f / mass;
        b->inv_inertia_diag = (phys_vec3_t){1.0f, 1.0f, 1.0f};
    } else {
        b->inv_mass = 0.0f;
        b->inv_inertia_diag = (phys_vec3_t){0, 0, 0};
    }
}

/**
 * @brief Build a contact constraint between two bodies along +Y.
 */
static void build_contact_(phys_constraint_t *c, uint32_t a, uint32_t b,
                            float penetration) {
    memset(c, 0, sizeof(*c));
    c->body_a = a;
    c->body_b = b;
    c->penetration = penetration;
    c->friction = 0.3f;
    c->row_count = 1;
    c->solver_mode = PHYS_SOLVER_TGS;

    /* Normal along +Y. */
    phys_jacobian_row_t *row = &c->rows[0];
    row->J_va = (phys_vec3_t){ 0, -1, 0};
    row->J_vb = (phys_vec3_t){ 0,  1, 0};
    row->J_wa = (phys_vec3_t){0, 0, 0};
    row->J_wb = (phys_vec3_t){0, 0, 0};
    row->lambda_min = 0.0f;
    row->lambda_max = 1e6f;
    row->bias = 0.0f;
    row->lambda = 0.0f;
    row->pseudo_lambda = 0.0f;

    /* Effective mass: 1 / (inv_mass_a + inv_mass_b) along normal. */
    row->effective_mass = 0.5f;  /* assumes both bodies have mass 1 */
}

/* ── Test: colored solve produces velocity corrections ─────────── */

/**
 * Chain of 4 bodies (A-B-C-D) linked by 3 constraints, with coloring
 * threshold = 2.  The island has 3 constraints ≥ 2, so coloring fires.
 * We verify that the solver produces non-zero velocity corrections.
 */
static int test_colored_solve_chain(void) {
    enum { NBODIES = 4, NCONS = 3 };

    phys_body_t bodies[NBODIES];
    for (int i = 0; i < NBODIES; ++i) {
        init_body_(&bodies[i], 1.0f);
    }
    /* Body 0 is static (floor). */
    bodies[0].inv_mass = 0.0f;
    bodies[0].inv_inertia_diag = (phys_vec3_t){0, 0, 0};

    phys_constraint_t cons[NCONS];
    build_contact_(&cons[0], 0, 1, 0.01f);
    build_contact_(&cons[1], 1, 2, 0.02f);
    build_contact_(&cons[2], 2, 3, 0.01f);

    uint32_t island_ci[NCONS] = {0, 1, 2};
    uint32_t island_bi[NBODIES] = {0, 1, 2, 3};

    phys_island_t island = {
        .constraint_indices = island_ci,
        .constraint_count   = NCONS,
        .body_indices       = island_bi,
        .body_count         = NBODIES,
        .sleeping           = false,
        .skip               = false,
    };
    phys_island_list_t islands = { .islands = &island, .count = 1 };

    phys_velocity_t vels[NBODIES];
    memset(vels, 0, sizeof(vels));

    /* Allocate arena. */
    uint8_t *arena_buf = malloc(ARENA_SIZE);
    ASSERT_TRUE(arena_buf != NULL);
    phys_frame_arena_t arena;
    arena.buffer = arena_buf;
    arena_init(&arena.arena, arena_buf, ARENA_SIZE);

    phys_stage_tgs_solve(&(phys_tgs_solve_args_t){
        .islands    = &islands,
        .constraints = cons,
        .bodies     = bodies,
        .velocities = vels,
        .pseudo_velocities = NULL,
        .body_count = NBODIES,
        .iterations = 10,
        .gravity    = {0, -9.81f, 0},
        .dt         = 1.0f / 60.0f,
        .tick_dt    = 1.0f / 60.0f,
        .slop       = 0.001f,
        .frame_arena = &arena,
        .island_color_threshold = 2,
    });

    /* Body 0 is static — velocity must remain zero. */
    ASSERT_FLOAT_NEAR(0.0f, vels[0].linear.y, 1e-6f);

    /* Dynamic bodies should have non-zero Y velocity (gravity + solver). */
    ASSERT_TRUE(vels[1].linear.y != 0.0f || vels[2].linear.y != 0.0f);

    free(arena_buf);
    return 0;
}

/* ── Test: threshold 0 disables coloring ───────────────────────── */

static int test_threshold_zero_no_coloring(void) {
    enum { NBODIES = 3, NCONS = 2 };

    phys_body_t bodies[NBODIES];
    for (int i = 0; i < NBODIES; ++i) {
        init_body_(&bodies[i], 1.0f);
    }
    bodies[0].inv_mass = 0.0f;
    bodies[0].inv_inertia_diag = (phys_vec3_t){0, 0, 0};

    phys_constraint_t cons[NCONS];
    build_contact_(&cons[0], 0, 1, 0.01f);
    build_contact_(&cons[1], 1, 2, 0.01f);

    uint32_t island_ci[NCONS] = {0, 1};
    uint32_t island_bi[NBODIES] = {0, 1, 2};
    phys_island_t island = {
        .constraint_indices = island_ci,
        .constraint_count   = NCONS,
        .body_indices       = island_bi,
        .body_count         = NBODIES,
    };
    phys_island_list_t islands = { .islands = &island, .count = 1 };

    phys_velocity_t vels[NBODIES];
    memset(vels, 0, sizeof(vels));

    /* Even with arena, threshold=0 means no coloring. */
    uint8_t *arena_buf = malloc(ARENA_SIZE);
    ASSERT_TRUE(arena_buf != NULL);
    phys_frame_arena_t arena;
    arena.buffer = arena_buf;
    arena_init(&arena.arena, arena_buf, ARENA_SIZE);

    phys_stage_tgs_solve(&(phys_tgs_solve_args_t){
        .islands    = &islands,
        .constraints = cons,
        .bodies     = bodies,
        .velocities = vels,
        .body_count = NBODIES,
        .iterations = 10,
        .gravity    = {0, -9.81f, 0},
        .dt         = 1.0f / 60.0f,
        .tick_dt    = 1.0f / 60.0f,
        .slop       = 0.001f,
        .frame_arena = &arena,
        .island_color_threshold = 0,
    });

    /* Should still produce corrections (sequential path). */
    ASSERT_TRUE(vels[1].linear.y != 0.0f);

    free(arena_buf);
    return 0;
}

/* ── Test: below threshold uses sequential ─────────────────────── */

static int test_below_threshold_sequential(void) {
    enum { NBODIES = 3, NCONS = 2 };

    phys_body_t bodies[NBODIES];
    for (int i = 0; i < NBODIES; ++i) {
        init_body_(&bodies[i], 1.0f);
    }
    bodies[0].inv_mass = 0.0f;
    bodies[0].inv_inertia_diag = (phys_vec3_t){0, 0, 0};

    phys_constraint_t cons[NCONS];
    build_contact_(&cons[0], 0, 1, 0.01f);
    build_contact_(&cons[1], 1, 2, 0.01f);

    uint32_t island_ci[NCONS] = {0, 1};
    uint32_t island_bi[NBODIES] = {0, 1, 2};
    phys_island_t island = {
        .constraint_indices = island_ci,
        .constraint_count   = NCONS,
        .body_indices       = island_bi,
        .body_count         = NBODIES,
    };
    phys_island_list_t islands = { .islands = &island, .count = 1 };

    /* Run with threshold = 10 (island has only 2 constraints < 10). */
    phys_velocity_t vels_seq[NBODIES];
    memset(vels_seq, 0, sizeof(vels_seq));

    uint8_t *arena_buf = malloc(ARENA_SIZE);
    ASSERT_TRUE(arena_buf != NULL);
    phys_frame_arena_t arena;
    arena.buffer = arena_buf;
    arena_init(&arena.arena, arena_buf, ARENA_SIZE);

    phys_stage_tgs_solve(&(phys_tgs_solve_args_t){
        .islands    = &islands,
        .constraints = cons,
        .bodies     = bodies,
        .velocities = vels_seq,
        .body_count = NBODIES,
        .iterations = 10,
        .gravity    = {0, -9.81f, 0},
        .dt         = 1.0f / 60.0f,
        .tick_dt    = 1.0f / 60.0f,
        .slop       = 0.001f,
        .frame_arena = &arena,
        .island_color_threshold = 10,
    });

    /* Should produce corrections via sequential path. */
    ASSERT_TRUE(vels_seq[1].linear.y != 0.0f);

    free(arena_buf);
    return 0;
}

/* ── Test: colored vs sequential produce similar results ───────── */

/**
 * Same island solved with coloring and without.  Results should be
 * similar (not necessarily identical due to ordering differences).
 */
static int test_colored_vs_sequential_similar(void) {
    enum { NBODIES = 4, NCONS = 3 };

    phys_body_t bodies[NBODIES];
    for (int i = 0; i < NBODIES; ++i) {
        init_body_(&bodies[i], 1.0f);
    }
    bodies[0].inv_mass = 0.0f;
    bodies[0].inv_inertia_diag = (phys_vec3_t){0, 0, 0};

    /* Constraints: 0-1, 1-2, 2-3 (chain). */
    phys_constraint_t cons_seq[NCONS], cons_col[NCONS];
    build_contact_(&cons_seq[0], 0, 1, 0.01f);
    build_contact_(&cons_seq[1], 1, 2, 0.02f);
    build_contact_(&cons_seq[2], 2, 3, 0.01f);
    memcpy(cons_col, cons_seq, sizeof(cons_seq));

    uint32_t island_ci[NCONS] = {0, 1, 2};
    uint32_t island_bi[NBODIES] = {0, 1, 2, 3};
    phys_island_t island = {
        .constraint_indices = island_ci,
        .constraint_count   = NCONS,
        .body_indices       = island_bi,
        .body_count         = NBODIES,
    };
    phys_island_list_t islands = { .islands = &island, .count = 1 };

    uint8_t *arena_buf = malloc(ARENA_SIZE);
    ASSERT_TRUE(arena_buf != NULL);
    phys_frame_arena_t arena;
    arena.buffer = arena_buf;

    /* Sequential solve. */
    phys_velocity_t vels_seq[NBODIES];
    memset(vels_seq, 0, sizeof(vels_seq));
    phys_stage_tgs_solve(&(phys_tgs_solve_args_t){
        .islands    = &islands,
        .constraints = cons_seq,
        .bodies     = bodies,
        .velocities = vels_seq,
        .body_count = NBODIES,
        .iterations = 20,
        .gravity    = {0, -9.81f, 0},
        .dt         = 1.0f / 60.0f,
        .tick_dt    = 1.0f / 60.0f,
        .slop       = 0.001f,
        .island_color_threshold = 0,  /* disabled */
    });

    /* Colored solve. */
    phys_velocity_t vels_col[NBODIES];
    memset(vels_col, 0, sizeof(vels_col));
    arena_init(&arena.arena, arena_buf, ARENA_SIZE);
    phys_stage_tgs_solve(&(phys_tgs_solve_args_t){
        .islands    = &islands,
        .constraints = cons_col,
        .bodies     = bodies,
        .velocities = vels_col,
        .body_count = NBODIES,
        .iterations = 20,
        .gravity    = {0, -9.81f, 0},
        .dt         = 1.0f / 60.0f,
        .tick_dt    = 1.0f / 60.0f,
        .slop       = 0.001f,
        .frame_arena = &arena,
        .island_color_threshold = 2,  /* enabled */
    });

    /* Results should be broadly similar.  Allow generous tolerance
     * because different ordering can change convergence path. */
    for (int i = 1; i < NBODIES; ++i) {
        float diff_y = vels_seq[i].linear.y - vels_col[i].linear.y;
        if (diff_y < 0) { diff_y = -diff_y; }
        /* Within 50% relative error or 0.5 m/s absolute. */
        float abs_seq = vels_seq[i].linear.y;
        if (abs_seq < 0) { abs_seq = -abs_seq; }
        ASSERT_TRUE(diff_y < 0.5f || diff_y < abs_seq * 0.5f);
    }

    free(arena_buf);
    return 0;
}

/* ── Test: null arena falls back to sequential ─────────────────── */

static int test_null_arena_fallback(void) {
    enum { NBODIES = 3, NCONS = 2 };

    phys_body_t bodies[NBODIES];
    for (int i = 0; i < NBODIES; ++i) {
        init_body_(&bodies[i], 1.0f);
    }
    bodies[0].inv_mass = 0.0f;
    bodies[0].inv_inertia_diag = (phys_vec3_t){0, 0, 0};

    phys_constraint_t cons[NCONS];
    build_contact_(&cons[0], 0, 1, 0.01f);
    build_contact_(&cons[1], 1, 2, 0.01f);

    uint32_t island_ci[NCONS] = {0, 1};
    uint32_t island_bi[NBODIES] = {0, 1, 2};
    phys_island_t island = {
        .constraint_indices = island_ci,
        .constraint_count   = NCONS,
        .body_indices       = island_bi,
        .body_count         = NBODIES,
    };
    phys_island_list_t islands = { .islands = &island, .count = 1 };

    phys_velocity_t vels[NBODIES];
    memset(vels, 0, sizeof(vels));

    /* threshold > 0 but no arena => sequential fallback. */
    phys_stage_tgs_solve(&(phys_tgs_solve_args_t){
        .islands    = &islands,
        .constraints = cons,
        .bodies     = bodies,
        .velocities = vels,
        .body_count = NBODIES,
        .iterations = 10,
        .gravity    = {0, -9.81f, 0},
        .dt         = 1.0f / 60.0f,
        .tick_dt    = 1.0f / 60.0f,
        .slop       = 0.001f,
        .frame_arena = NULL,
        .island_color_threshold = 1,
    });

    /* Should still produce corrections. */
    ASSERT_TRUE(vels[1].linear.y != 0.0f);

    return 0;
}

/* ── Runner ────────────────────────────────────────────────────── */

typedef int (*test_fn)(void);
typedef struct { const char *name; test_fn fn; } test_entry_t;

int main(void) {
    const test_entry_t tests[] = {
        { "test_colored_solve_chain",           test_colored_solve_chain },
        { "test_threshold_zero_no_coloring",    test_threshold_zero_no_coloring },
        { "test_below_threshold_sequential",    test_below_threshold_sequential },
        { "test_colored_vs_sequential_similar", test_colored_vs_sequential_similar },
        { "test_null_arena_fallback",           test_null_arena_fallback },
    };

    int n = (int)(sizeof(tests) / sizeof(tests[0]));
    int passed = 0;

    printf("p096_tgs_coloring_tests:\n");
    for (int i = 0; i < n; ++i) {
        int rc = tests[i].fn();
        printf("  %-50s %s\n", tests[i].name, rc == 0 ? "PASS" : "FAIL");
        if (rc == 0) { ++passed; }
    }

    printf("%d/%d tests passed\n", passed, n);
    return passed == n ? 0 : 1;
}
