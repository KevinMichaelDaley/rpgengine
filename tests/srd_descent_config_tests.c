/**
 * @file srd_descent_config_tests.c
 * @brief Tests for SRD budget-driven configuration (srd_descent_config_t).
 *
 * Non-static functions (1): main
 */
#include "ferrum/procgen/srd/srd_descent_config.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

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

#define ASSERT_FLOAT_EQ(a, b, eps) ASSERT(fabsf((float)(a) - (float)(b)) < (eps))

/* ── Budget tier tests ─────────────────────────────────────────── */

TEST(test_tier_under_2s) {
    srd_descent_config_t cfg;
    srd_descent_config_from_budget(&cfg, 1.0);
    ASSERT(cfg.k_candidates == 16);
    ASSERT(cfg.lbfgs_max_iter == 20);
    ASSERT(cfg.local_optimize_steps == 3);
    ASSERT_FLOAT_EQ(cfg.time_budget_s, 1.0, 0.001);
}

TEST(test_tier_2_to_10s) {
    srd_descent_config_t cfg;
    srd_descent_config_from_budget(&cfg, 5.0);
    ASSERT(cfg.k_candidates == 64);
    ASSERT(cfg.lbfgs_max_iter == 100);
    ASSERT(cfg.local_optimize_steps == 10);
}

TEST(test_tier_10_to_60s) {
    srd_descent_config_t cfg;
    srd_descent_config_from_budget(&cfg, 30.0);
    ASSERT(cfg.k_candidates == 256);
    ASSERT(cfg.lbfgs_max_iter == 500);
    ASSERT(cfg.local_optimize_steps == 25);
}

TEST(test_tier_over_60s) {
    srd_descent_config_t cfg;
    srd_descent_config_from_budget(&cfg, 120.0);
    ASSERT(cfg.k_candidates == 512);
    /* "until convergence" means max_iter very high */
    ASSERT(cfg.lbfgs_max_iter >= 10000);
    ASSERT(cfg.local_optimize_steps == 50);
}

/* ── Shared defaults ──────────────────────────────────────────── */

TEST(test_temperature_defaults) {
    srd_descent_config_t cfg;
    srd_descent_config_from_budget(&cfg, 5.0);
    ASSERT_FLOAT_EQ(cfg.temperature_init, 1.0f, 0.001f);
    ASSERT_FLOAT_EQ(cfg.temperature_decay, 0.995f, 0.001f);
    ASSERT_FLOAT_EQ(cfg.temperature_min, 0.01f, 0.001f);
}

TEST(test_lbfgs_defaults) {
    srd_descent_config_t cfg;
    srd_descent_config_from_budget(&cfg, 5.0);
    ASSERT(cfg.lbfgs_history_size == 10);
    ASSERT_FLOAT_EQ(cfg.lbfgs_tolerance_grad, 1e-5f, 1e-7f);
    ASSERT_FLOAT_EQ(cfg.lbfgs_tolerance_change, 1e-9f, 1e-11f);
}

TEST(test_rules_and_critic_null) {
    srd_descent_config_t cfg;
    srd_descent_config_from_budget(&cfg, 5.0);
    ASSERT(cfg.rules == NULL);
    ASSERT(cfg.critic == NULL);
}

/* ── Boundary values ─────────────────────────────────────────── */

TEST(test_boundary_exactly_2s) {
    srd_descent_config_t cfg;
    srd_descent_config_from_budget(&cfg, 2.0);
    ASSERT(cfg.k_candidates == 64);
}

TEST(test_boundary_exactly_10s) {
    srd_descent_config_t cfg;
    srd_descent_config_from_budget(&cfg, 10.0);
    ASSERT(cfg.k_candidates == 256);
}

TEST(test_boundary_exactly_60s) {
    srd_descent_config_t cfg;
    srd_descent_config_from_budget(&cfg, 60.0);
    ASSERT(cfg.k_candidates == 512);
}

TEST(test_continuous_steps_per_rewrite) {
    /* Each tier should have a reasonable continuous_steps_per_rewrite */
    srd_descent_config_t cfg;
    srd_descent_config_from_budget(&cfg, 1.0);
    ASSERT(cfg.continuous_steps_per_rewrite > 0);

    srd_descent_config_from_budget(&cfg, 120.0);
    ASSERT(cfg.continuous_steps_per_rewrite > 0);
}

/* ── Main ─────────────────────────────────────────────────────── */

int main(void) {
    printf("=== SRD Descent Config Tests ===\n");

    RUN(test_tier_under_2s);
    RUN(test_tier_2_to_10s);
    RUN(test_tier_10_to_60s);
    RUN(test_tier_over_60s);
    RUN(test_temperature_defaults);
    RUN(test_lbfgs_defaults);
    RUN(test_rules_and_critic_null);
    RUN(test_boundary_exactly_2s);
    RUN(test_boundary_exactly_10s);
    RUN(test_boundary_exactly_60s);
    RUN(test_continuous_steps_per_rewrite);

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
