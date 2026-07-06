/**
 * @file srd_continuous_phase.h
 * @brief L-BFGS continuous optimisation phase for SRD.
 *
 * Extracts layout parameters into a libtorch tensor, runs L-BFGS
 * using the critic as the loss function, and writes results back.
 * Factored out of the main SRD loop for testability.
 *
 * Non-static functions declared (1): srd_continuous_phase_run
 */
#ifndef FERRUM_PROCGEN_SRD_CONTINUOUS_PHASE_H
#define FERRUM_PROCGEN_SRD_CONTINUOUS_PHASE_H

#include "ferrum/procgen/srd/srd_sdf_layout.h"
#include "ferrum/procgen/srd/srd_descent_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Run one continuous optimisation phase (L-BFGS).
 *
 * Extracts (cx, cz, hw, hd) from layout->boxes into a [N,4] tensor
 * with requires_grad=true. Runs L-BFGS for cfg->lbfgs_max_iter
 * iterations using cfg->critic as the loss function. Writes optimised
 * parameters back to layout->boxes, clamping hw/hd >= SRD_EPSILON.
 *
 * @param layout  Layout to optimise in-place. Must not be NULL.
 * @param cfg     Configuration with critic, LBFGS params. Must not be NULL.
 *                cfg->critic must be set.
 * @return Final loss value, or -1.0f on error.
 *
 * @note Ownership: layout is modified in-place. cfg is read-only.
 * @note Side effects: modifies layout->boxes[*].{cx,cz,hw,hd}.
 */
float srd_continuous_phase_run(srd_sdf_layout_t *layout,
                               const srd_descent_config_t *cfg);

#ifdef __cplusplus
}
#endif
#endif /* FERRUM_PROCGEN_SRD_CONTINUOUS_PHASE_H */
