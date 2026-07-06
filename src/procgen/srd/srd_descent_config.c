/**
 * @file srd_descent_config.c
 * @brief Budget-driven SRD configuration: maps time budget to parameters.
 *
 * Non-static functions (1): srd_descent_config_from_budget
 */
#include "ferrum/procgen/srd/srd_descent_config.h"

#include <string.h>

void srd_descent_config_from_budget(srd_descent_config_t *cfg, double budget_s) {
    if (!cfg) return;
    memset(cfg, 0, sizeof(*cfg));

    cfg->time_budget_s = budget_s;

    /* Shared defaults across all tiers */
    cfg->lbfgs_history_size     = 10;
    cfg->lbfgs_tolerance_grad   = 1e-5f;
    cfg->lbfgs_tolerance_change = 1e-9f;
    cfg->temperature_init       = 1.0f;
    cfg->temperature_decay      = 0.995f;
    cfg->temperature_min        = 0.01f;

    /* Tier-specific parameters */
    if (budget_s < 2.0) {
        cfg->k_candidates               = 16;
        cfg->lbfgs_max_iter             = 20;
        cfg->local_optimize_steps       = 3;
        cfg->continuous_steps_per_rewrite = 1;
    } else if (budget_s < 10.0) {
        cfg->k_candidates               = 64;
        cfg->lbfgs_max_iter             = 100;
        cfg->local_optimize_steps       = 10;
        cfg->continuous_steps_per_rewrite = 3;
    } else if (budget_s < 60.0) {
        cfg->k_candidates               = 256;
        cfg->lbfgs_max_iter             = 500;
        cfg->local_optimize_steps       = 25;
        cfg->continuous_steps_per_rewrite = 5;
    } else {
        cfg->k_candidates               = 512;
        cfg->lbfgs_max_iter             = 100000; /* "until convergence" */
        cfg->local_optimize_steps       = 50;
        cfg->continuous_steps_per_rewrite = 10;
    }

    /* Caller must set rules and critic */
    cfg->rules  = NULL;
    cfg->critic = NULL;
}
