#ifndef FERRUM_PROCGEN_SRD_OPTIMIZER_H
#define FERRUM_PROCGEN_SRD_OPTIMIZER_H

#include <stdint.h>
#include "ferrum/procgen/procgen_srd_types.h"
#include "ferrum/procgen/srd/srd_loss_compiler.h"
#include "ferrum/procgen/srd/srd_anneal.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    double  time_budget_s;
    int     max_steps;
    int     rewrite_interval;
    double  lr;
    double  min_improvement;
} srd_optimize_config_t;

void srd_optimize_config_default(srd_optimize_config_t *cfg);

/**
 * @brief Run SRD optimization on a set of rooms.
 *
 * Alternates between perturbation-based continuous optimization
 * and structural rewrites, minimizing the given loss terms.
 *
 * @param rooms      Room array (modified in place).
 * @param n_rooms    Number of rooms (updated after rewrites).
 * @param cap        Capacity of rooms array.
 * @param corridors  Corridor array.
 * @param n_corridors Number of corridors.
 * @param terms      Compiled loss terms.
 * @param n_terms    Number of loss terms.
 * @param config     Optimization configuration.
 * @return Total energy after optimization.
 */
double srd_optimize(fr_room_box_t *rooms, uint32_t *n_rooms, uint32_t cap,
                    fr_corridor_seg_t *corridors, uint32_t *n_corridors,
                    const srd_loss_term_t *terms, uint32_t n_terms,
                    const srd_optimize_config_t *config);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_PROCGEN_SRD_OPTIMIZER_H */
