/**
 * @file srd_discrete_candidates.h
 * @brief Candidate sampling and LocalOptimize for the SRD discrete phase.
 *
 * Samples K valid rule/selection pairs, applies each to a layout copy,
 * runs L-BFGS LocalOptimize, and records delta_L. All storage is
 * stack-allocated (no heap allocation inside the sampling loop).
 *
 * Non-static functions declared (1): srd_discrete_sample_candidates
 */
#ifndef FERRUM_PROCGEN_SRD_DISCRETE_CANDIDATES_H
#define FERRUM_PROCGEN_SRD_DISCRETE_CANDIDATES_H

#include "ferrum/procgen/srd/srd_descent_rules.h"
#include "ferrum/procgen/srd/srd_sdf_layout.h"
#include "ferrum/procgen/srd/srd_descent_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum candidates per discrete phase (matches max budget tier). */
#define SRD_K_MAX 512

/**
 * @brief One candidate from the discrete sampling phase.
 *
 * Contains the rule applied, the selection, a full layout copy after
 * apply + LocalOptimize, and the measured loss improvement.
 *
 * @note This struct is large (~260 KB due to layout_copy). Arrays of
 *       these should not be stack-allocated at full SRD_K_MAX size
 *       in fiber contexts; use heap or arena allocation for large K.
 */
typedef struct {
    int             rule_idx;     /**< Index of rule applied */
    srd_selection_t sel;          /**< Selection used */
    srd_sdf_layout_t layout_copy; /**< Layout after apply + LocalOptimize */
    float           delta_L;     /**< Loss improvement: current_loss - post_loss.
                                      Positive means improvement.
                                      -INFINITY means apply failed. */
} srd_candidate_t;

/**
 * @brief Sample K candidates by applying random rules and running LocalOptimize.
 *
 * For each of K iterations:
 *   1. Find applicable (non-repair) rules
 *   2. Pick one uniformly at random
 *   3. Sample a valid selection
 *   4. Copy the layout, apply the rule
 *   5. Run local_optimize_steps of L-BFGS on the copy
 *   6. Record delta_L = current_loss - post_loss
 *
 * If no applicable rules exist or rule.apply fails, the candidate gets
 * delta_L = -INFINITY and is skipped in downstream processing.
 *
 * @param layout      Current layout (read-only, not modified).
 * @param cfg         Configuration with critic, rules, local_optimize_steps.
 *                    cfg->rules and cfg->critic must be set.
 * @param out         Output array of candidates. Must have capacity >= k.
 * @param k           Number of candidates to sample.
 * @param rng_state   Pointer to xorshift32 RNG state (modified).
 * @return Number of candidates written to out (always == k).
 *
 * @note Ownership: out is caller-owned. layout is not modified.
 * @note Side effects: modifies rng_state.
 */
int srd_discrete_sample_candidates(const srd_sdf_layout_t *layout,
                                   const srd_descent_config_t *cfg,
                                   srd_candidate_t *out,
                                   int k,
                                   uint32_t *rng_state);

#ifdef __cplusplus
}
#endif
#endif /* FERRUM_PROCGEN_SRD_DISCRETE_CANDIDATES_H */
