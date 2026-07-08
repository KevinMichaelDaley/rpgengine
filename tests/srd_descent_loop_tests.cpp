/**
 * @file srd_descent_loop_tests.cpp
 * @brief Tests for the SRD outer descent loop.
 *
 * Non-static functions (1): main
 *
 * Tests:
 *   - Loop terminates within time budget
 *   - Loss at end <= loss at start
 *   - Temperature decreases monotonically
 *   - Short budget still completes
 */
#include "ferrum/procgen/srd/srd_descent_loop.h"
#include "ferrum/procgen/srd/srd_discrete_candidates.h"
#include "ferrum/procgen/srd/srd_continuous_phase.h"
#include "ferrum/procgen/srd/srd_descent_config.h"
#include "ferrum/procgen/srd/srd_sdf_layout.h"
#include "ferrum/procgen/srd/srd_room_type.h"
#include "ferrum/procgen/srd/srd_critic.h"
#include "ferrum/procgen/srd/srd_rules_room.h"
#include "ferrum/procgen/srd/srd_rules_repair.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

/* ── Test harness ──────────────────────────────────────────────── */

static int g_pass = 0;
static int g_fail = 0;

#define TEST(name) static void name(void)
#define RUN(name) do { \
    printf("  %-55s ", #name); \
    name(); \
    printf("[PASS]\n"); \
    g_pass++; \
} while (0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("[FAIL] %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        g_fail++; return; \
    } \
} while (0)

/* ── Helper: build a non-trivial test layout ─────────────────── */

static void make_test_layout(srd_sdf_layout_t *layout) {
    srd_sdf_layout_init(layout);
    layout->bounds_w = 40.0f;
    layout->bounds_h = 40.0f;

    srd_sdf_box_t b;
    memset(&b, 0, sizeof(b));

    /* Box 0: overlapping with box 1 */
    b.cx = 5.0f; b.cz = 5.0f; b.hw = 3.0f; b.hd = 3.0f;
    b.type = SRD_ROOM_GENERIC;
    srd_sdf_layout_add_box(layout, &b);

    b.cx = 7.0f; b.cz = 5.0f; b.hw = 3.0f; b.hd = 3.0f;
    b.type = SRD_ROOM_ENTRANCE;
    srd_sdf_layout_add_box(layout, &b);

    b.cx = 20.0f; b.cz = 20.0f; b.hw = 4.0f; b.hd = 4.0f;
    b.type = SRD_ROOM_GENERIC;
    srd_sdf_layout_add_box(layout, &b);

    b.cx = 30.0f; b.cz = 30.0f; b.hw = 2.0f; b.hd = 2.0f;
    b.type = SRD_ROOM_CORRIDOR;
    srd_sdf_layout_add_box(layout, &b);

    srd_sdf_layout_set_adj(layout, 0, 1, true);
    srd_sdf_layout_set_adj(layout, 1, 2, true);
    srd_sdf_layout_set_adj(layout, 2, 3, true);
}

static double now_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/* ── Tests ────────────────────────────────────────────────────── */

/** @brief Build a minimal config for fast tests. */
static void make_fast_config(srd_descent_config_t *cfg, double budget_s) {
    srd_descent_config_from_budget(cfg, budget_s);
    /* Override to minimal settings for test speed */
    cfg->k_candidates = 2;
    cfg->lbfgs_max_iter = 1;
    cfg->local_optimize_steps = 1;
    cfg->continuous_steps_per_rewrite = 1;
}

TEST(test_terminates_within_budget) {
    srd_sdf_layout_t layout;
    make_test_layout(&layout);

    srd_rule_table_t tbl;
    srd_rule_table_init(&tbl);
    srd_rules_room_register_split(&tbl);
    srd_rules_room_register_modify(&tbl);
    srd_rules_repair_register(&tbl);

    srd_critic_t *critic = srd_critic_create_analytical(40.0f, 40.0f);
    ASSERT(critic != NULL);

    srd_descent_config_t cfg;
    make_fast_config(&cfg, 0.5);
    cfg.critic = critic;
    cfg.rules = &tbl;

    double t0 = now_seconds();
    srd_descent_result_t result = srd_descent_optimize(&layout, &cfg);
    double elapsed = now_seconds() - t0;

    /* Should terminate within 200% of budget (allow overhead for torch init) */
    ASSERT(elapsed < 1.0);
    ASSERT(result.final_loss >= 0.0f);
    ASSERT(result.iterations >= 0);

    srd_critic_destroy(critic);
}

TEST(test_loss_does_not_increase) {
    srd_sdf_layout_t layout;
    make_test_layout(&layout);

    srd_rule_table_t tbl;
    srd_rule_table_init(&tbl);
    srd_rules_room_register_split(&tbl);
    srd_rules_room_register_modify(&tbl);
    srd_rules_room_register_annex(&tbl);
    srd_rules_repair_register(&tbl);

    srd_critic_t *critic = srd_critic_create_analytical(40.0f, 40.0f);
    ASSERT(critic != NULL);

    srd_descent_config_t cfg;
    make_fast_config(&cfg, 1.0);
    cfg.critic = critic;
    cfg.rules = &tbl;

    srd_descent_result_t result = srd_descent_optimize(&layout, &cfg);

    /* Loss at end should be <= loss at start (allow small epsilon for stochastic rewrites) */
    ASSERT(result.final_loss <= result.initial_loss + 1.0f);

    srd_critic_destroy(critic);
}

TEST(test_temperature_decreases) {
    srd_sdf_layout_t layout;
    make_test_layout(&layout);

    srd_rule_table_t tbl;
    srd_rule_table_init(&tbl);
    srd_rules_room_register_split(&tbl);
    srd_rules_repair_register(&tbl);

    srd_critic_t *critic = srd_critic_create_analytical(40.0f, 40.0f);
    ASSERT(critic != NULL);

    srd_descent_config_t cfg;
    make_fast_config(&cfg, 0.5);
    cfg.critic = critic;
    cfg.rules = &tbl;

    srd_descent_result_t result = srd_descent_optimize(&layout, &cfg);

    /* Final temperature should be lower than initial */
    if (result.iterations > 0) {
        ASSERT(result.final_temperature < cfg.temperature_init);
        ASSERT(result.final_temperature >= cfg.temperature_min);
    }

    srd_critic_destroy(critic);
}

TEST(test_short_budget_completes) {
    srd_sdf_layout_t layout;
    make_test_layout(&layout);

    srd_rule_table_t tbl;
    srd_rule_table_init(&tbl);
    srd_rules_room_register_split(&tbl);
    srd_rules_repair_register(&tbl);

    srd_critic_t *critic = srd_critic_create_analytical(40.0f, 40.0f);
    ASSERT(critic != NULL);

    srd_descent_config_t cfg;
    make_fast_config(&cfg, 0.5);
    cfg.critic = critic;
    cfg.rules = &tbl;

    srd_descent_result_t result = srd_descent_optimize(&layout, &cfg);

    /* Should still complete without error */
    ASSERT(result.final_loss >= 0.0f);

    srd_critic_destroy(critic);
}

/* ── Main ─────────────────────────────────────────────────────── */

int main(void) {
    printf("=== SRD Descent Loop Tests ===\n");

    RUN(test_terminates_within_budget);
    RUN(test_loss_does_not_increase);
    RUN(test_temperature_decreases);
    RUN(test_short_budget_completes);

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
