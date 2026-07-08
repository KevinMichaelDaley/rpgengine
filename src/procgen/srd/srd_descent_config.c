/**
 * @file srd_descent_config.c
 * @brief Budget-driven SRD configuration for the voxel-grid descent loop.
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
    cfg->temperature_init  = 1.0f;
    cfg->temperature_decay = 0.995f;
    cfg->temperature_min   = 0.01f;

    /* Default critic config */
    srd_grid_critic_config_default(&cfg->critic_cfg);

    /* Tier-specific parameters */
    if (budget_s < 2.0) {
        cfg->k_candidates = 8;
    } else if (budget_s < 10.0) {
        cfg->k_candidates = 16;
    } else if (budget_s < 60.0) {
        cfg->k_candidates = 32;
    } else {
        cfg->k_candidates = 64;
    }

    /* Caller must set rules and n_rules */
    cfg->rules   = NULL;
    cfg->n_rules = 0;
    cfg->verbose = 0;
}
