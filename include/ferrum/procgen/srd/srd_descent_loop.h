/**
 * @file srd_descent_loop.h
 * @brief Pure discrete SRD descent loop on a voxel SDF grid.
 *
 * Each iteration: sample K candidate (rule, selection) pairs, evaluate
 * each on a grid copy via the grid-based critic, accept the best if it
 * improves loss, anneal temperature. Terminates when time_budget_s is
 * exhausted.
 *
 * Non-static functions declared (1): srd_descent_optimize
 */
#ifndef FERRUM_PROCGEN_SRD_DESCENT_LOOP_H
#define FERRUM_PROCGEN_SRD_DESCENT_LOOP_H

#include "ferrum/procgen/srd/srd_sdf_grid.h"
#include "ferrum/procgen/srd/srd_room_map.h"
#include "ferrum/procgen/srd/srd_descent_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Result of a full SRD optimisation run.
 */
typedef struct {
    float  initial_loss;       /**< Loss before optimisation. */
    float  final_loss;         /**< Loss after optimisation. */
    float  final_temperature;  /**< Temperature at termination. */
    int    iterations;         /**< Number of outer loop iterations completed. */
} srd_descent_result_t;

/**
 * @brief Run the pure discrete descent loop on a voxel SDF grid.
 *
 * Each iteration:
 *   1. Sample K random (rule, selection) pairs from cfg->rules.
 *   2. For each candidate: copy grid+map, apply rule, evaluate loss.
 *   3. Accept the best candidate if it reduces loss (or passes
 *      temperature-gated acceptance).
 *   4. Anneal temperature: T *= decay, clamped to T_min.
 *
 * Terminates when elapsed time >= time_budget_s.
 *
 * @param grid  SDF grid to optimise in-place. Must not be NULL.
 * @param map   Room map to optimise in-place. Must not be NULL.
 * @param cfg   Configuration with critic, rules, budget. Must not be NULL.
 *              cfg->rules and cfg->n_rules must be set.
 * @return Result struct with initial/final loss, temperature, iteration count.
 *         Returns final_loss = -1 on invalid input.
 *
 * @note Ownership: grid and map are modified in-place. cfg is read-only.
 */
srd_descent_result_t srd_descent_optimize(srd_sdf_grid_t *grid,
                                          srd_room_map_t *map,
                                          const srd_descent_config_t *cfg);

#ifdef __cplusplus
}
#endif
#endif /* FERRUM_PROCGEN_SRD_DESCENT_LOOP_H */
