/**
 * @file srd_grid_critic.c
 * @brief Grid-based critic: configuration and evaluation entry point.
 *
 * Non-static functions (2):
 *   srd_grid_critic_config_default
 *   srd_grid_critic_evaluate
 */
#include "ferrum/procgen/srd/srd_grid_critic.h"

#include <string.h>

/* ── Term functions (srd_grid_critic_terms.c) ─────────────────────── */

float srd_grid_critic_volume(const srd_room_map_t *map, int min_room_voxels);
float srd_grid_critic_reachability(const srd_sdf_grid_t *grid,
                                   const srd_room_map_t *map);
float srd_grid_critic_bounds(const srd_sdf_grid_t *grid);
float srd_grid_critic_separation(const srd_room_map_t *map);

/* ── Public API ───────────────────────────────────────────────────── */

void srd_grid_critic_config_default(srd_grid_critic_config_t *cfg) {
    if (!cfg) return;
    cfg->w_volume       = 1.0f;
    cfg->w_reachability = 2.0f;
    cfg->w_bounds       = 1.0f;
    cfg->w_separation   = 1.0f;
    cfg->min_room_voxels = 64;
}

srd_grid_critic_result_t srd_grid_critic_evaluate(
    const srd_sdf_grid_t *grid,
    const srd_room_map_t *map,
    const srd_grid_critic_config_t *cfg)
{
    srd_grid_critic_result_t r;
    memset(&r, 0, sizeof(r));

    if (!grid || !map || !cfg) return r;

    r.volume       = srd_grid_critic_volume(map, cfg->min_room_voxels);
    r.reachability = srd_grid_critic_reachability(grid, map);
    r.bounds       = srd_grid_critic_bounds(grid);
    r.separation   = srd_grid_critic_separation(map);

    r.total = cfg->w_volume       * r.volume
            + cfg->w_reachability * r.reachability
            + cfg->w_bounds       * r.bounds
            + cfg->w_separation   * r.separation;

    return r;
}
