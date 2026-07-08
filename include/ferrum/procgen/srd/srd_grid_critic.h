/**
 * @file srd_grid_critic.h
 * @brief Grid-based critic for SRD voxel SDF layouts.
 *
 * Evaluates a layout (SDF grid + room map) against quality criteria,
 * returning a weighted loss with per-term breakdowns. Loss terms:
 *   - volume:       rooms below minimum voxel count
 *   - reachability: rooms unreachable from entrance via flood-fill
 *   - bounds:       room interior touching grid boundary
 *   - separation:   bad room-type adjacency (boss next to entrance, etc.)
 *
 * Types (2): srd_grid_critic_config_t, srd_grid_critic_result_t
 */
#ifndef SRD_GRID_CRITIC_H
#define SRD_GRID_CRITIC_H

#include "ferrum/procgen/srd/srd_sdf_grid.h"
#include "ferrum/procgen/srd/srd_room_map.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Configuration for the grid-based critic.
 *
 * Weights control the relative importance of each loss term.
 * All weights must be >= 0.
 */
typedef struct {
    float w_volume;       /**< Weight for minimum-volume penalty. */
    float w_reachability; /**< Weight for reachability penalty. */
    float w_bounds;       /**< Weight for bounds-violation penalty. */
    float w_separation;   /**< Weight for type-separation penalty. */
    int   min_room_voxels;/**< Minimum acceptable room volume in voxels. */
} srd_grid_critic_config_t;

/**
 * @brief Result of a critic evaluation, with per-term and total loss.
 *
 * Each term is >= 0. Total = weighted sum of all terms.
 */
typedef struct {
    float total;       /**< Weighted sum of all loss terms. */
    float volume;      /**< Volume penalty (rooms too small). */
    float reachability;/**< Reachability penalty (disconnected rooms). */
    float bounds;      /**< Bounds violation (interior at grid edge). */
    float separation;  /**< Type separation penalty (bad adjacency). */
} srd_grid_critic_result_t;

/* ── Configuration (srd_grid_critic.c) ────────────────────────────── */

/**
 * @brief Fill a config with sensible default values.
 *
 * @param cfg Output config. Must not be NULL.
 */
void srd_grid_critic_config_default(srd_grid_critic_config_t *cfg);

/* ── Evaluation (srd_grid_critic.c, srd_grid_critic_terms.c) ─────── */

/**
 * @brief Evaluate a layout against all critic terms.
 *
 * Returns a zero-total result if any input is NULL.
 *
 * @param grid SDF grid (may be NULL -> zero result).
 * @param map  Room map (may be NULL -> zero result).
 * @param cfg  Critic config (may be NULL -> zero result).
 * @return Evaluation result with per-term and total loss.
 */
srd_grid_critic_result_t srd_grid_critic_evaluate(
    const srd_sdf_grid_t *grid,
    const srd_room_map_t *map,
    const srd_grid_critic_config_t *cfg);

#ifdef __cplusplus
}
#endif

#endif /* SRD_GRID_CRITIC_H */
