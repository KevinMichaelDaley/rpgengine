/**
 * @file srd_descent_config.h
 * @brief Budget-driven configuration for the SRD optimiser loop.
 *
 * Maps a time budget to appropriate parameters for the L-BFGS
 * continuous optimiser and discrete rewrite phase. Four tiers:
 *   <2s, 2-10s, 10-60s, >60s.
 *
 * Non-static functions declared (1): srd_descent_config_from_budget
 */
#ifndef FERRUM_PROCGEN_SRD_DESCENT_CONFIG_H
#define FERRUM_PROCGEN_SRD_DESCENT_CONFIG_H

#include "ferrum/procgen/srd/srd_descent_rules.h"
#include "ferrum/procgen/srd/srd_critic.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Full configuration for the SRD optimiser loop.
 *
 * Populated by srd_descent_config_from_budget. Caller must set
 * rules and critic pointers before passing to the SRD loop.
 */
typedef struct {
    /** Time budget — drives all other defaults. */
    double time_budget_s;

    /** @name Continuous optimiser (L-BFGS via libtorch) */
    /**@{*/
    int   lbfgs_max_iter;         /**< Iterations per continuous phase */
    int   lbfgs_history_size;     /**< Quasi-Newton history (default 10) */
    float lbfgs_tolerance_grad;   /**< Convergence threshold (default 1e-5) */
    float lbfgs_tolerance_change; /**< Loss change threshold (default 1e-9) */
    /**@}*/

    /** @name Discrete rewrite phase */
    /**@{*/
    int k_candidates;               /**< Candidate rules to evaluate per step */
    int continuous_steps_per_rewrite;/**< L-BFGS rounds between discrete steps */
    int local_optimize_steps;       /**< L-BFGS steps in candidate LocalOptimize */
    /**@}*/

    /** @name Temperature annealing */
    /**@{*/
    float temperature_init;  /**< Starting temperature (default 1.0) */
    float temperature_decay; /**< Multiplied each outer iteration (default 0.995) */
    float temperature_min;   /**< Floor temperature (default 0.01) */
    /**@}*/

    /** @name Rule table and critic (caller owns both) */
    /**@{*/
    srd_rule_table_t *rules;  /**< Rule table — set by caller */
    srd_critic_t     *critic; /**< Critic — set by caller */
    /**@}*/
} srd_descent_config_t;

/**
 * @brief Populate config with budget-appropriate defaults.
 *
 * Fills all fields based on the time budget tier. The rules and
 * critic pointers are set to NULL — caller must assign them.
 *
 * Budget tiers:
 *   | Budget  | K  | L-BFGS iters | LocalOpt steps |
 *   |---------|-----|-------------|----------------|
 *   | < 2s    | 16  | 20          | 3              |
 *   | 2-10s   | 64  | 100         | 10             |
 *   | 10-60s  | 256 | 500         | 25             |
 *   | > 60s   | 512 | convergence | 50             |
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
