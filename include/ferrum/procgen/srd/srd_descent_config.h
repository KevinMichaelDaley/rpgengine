/**
 * @file srd_descent_config.h
 * @brief Configuration for the voxel-grid SRD optimiser loop.
 *
 * Budget-driven configuration for the pure discrete rewrite loop.
 * No L-BFGS or libtorch — uses the grid-based critic and voxel
 * rewrite rules directly.
 *
 * Non-static functions declared (1): srd_descent_config_from_budget
 */
#ifndef FERRUM_PROCGEN_SRD_DESCENT_CONFIG_H
#define FERRUM_PROCGEN_SRD_DESCENT_CONFIG_H

#include "ferrum/procgen/srd/srd_grid_critic.h"
#include "ferrum/procgen/srd/srd_voxel_rule_table.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Full configuration for the voxel-grid SRD optimiser loop.
 *
 * Populated by srd_descent_config_from_budget. Caller must set
 * rules and n_rules before passing to the loop.
 */
typedef struct {
    /** Time budget — drives all other defaults. */
    double time_budget_s;

    /** @name Discrete rewrite phase */
    /**@{*/
    int k_candidates;  /**< Candidate rules to evaluate per iteration. */
    /**@}*/

    /** @name Temperature annealing */
    /**@{*/
    float temperature_init;  /**< Starting temperature (default 1.0). */
    float temperature_decay; /**< Multiplied each iteration (default 0.995). */
    float temperature_min;   /**< Floor temperature (default 0.01). */
    /**@}*/

    /** @name Grid critic */
    /**@{*/
    srd_grid_critic_config_t critic_cfg; /**< Critic weights and thresholds. */
    /**@}*/

    /** @name Rule table (caller owns the array) */
    /**@{*/
    const srd_voxel_rule_entry_t *rules; /**< Rule table — set by caller. */
    int n_rules;                          /**< Number of entries in rules. */
    /**@}*/

    /** @name Logging */
    /**@{*/
    int verbose; /**< If nonzero, print loss every 10 iterations. */
    /**@}*/
} srd_descent_config_t;

/**
 * @brief Populate config with budget-appropriate defaults.
 *
 * Fills all fields based on the time budget tier. The rules pointer
 * and n_rules are set to 0/NULL — caller must assign them.
 *
 * Budget tiers:
 *   | Budget  | K  |
 *   |---------|-----|
 *   | < 2s    | 8   |
 *   | 2-10s   | 16  |
 *   | 10-60s  | 32  |
 *   | > 60s   | 64  |
 *
 * @param[out] cfg       Config to populate. Must not be NULL.
 * @param[in]  budget_s  Time budget in seconds (positive).
 *
 * @note Ownership: cfg is caller-owned, written in-place.
 * @note Side effects: none.
 */
void srd_descent_config_from_budget(srd_descent_config_t *cfg, double budget_s);

#ifdef __cplusplus
}
#endif
#endif /* FERRUM_PROCGEN_SRD_DESCENT_CONFIG_H */
