/**
 * @file npc_nav_world.h
 * @brief Navigation world: SVO + hierarchical graph + dynamic blockers.
 */

#ifndef FERRUM_NPC_NAV_WORLD_H
#define FERRUM_NPC_NAV_WORLD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/npc/npc_svo.h"
#include "ferrum/npc/npc_nav_graph.h"
#include "ferrum/npc/npc_pathfind.h"

typedef struct npc_nav_world {
    npc_svo_grid_t     svo;
    npc_nav_graph_t    graph;
    npc_nav_hgraph_t   hgraph;
    npc_svo_blocker_t *dynamic_blockers;
    uint32_t           blocker_count;
    uint32_t           blocker_cap;
    bool               built;
} npc_nav_world_t;

/**
 * @brief Initialise a nav world. Does NOT build the graph yet.
 */
bool npc_nav_world_init(npc_nav_world_t *nw);

/**
 * @brief Destroy and free all resources.
 */
void npc_nav_world_destroy(npc_nav_world_t *nw);

/**
 * @brief Execute a nav query from packed task params.
 *
 * Param layout (64 bytes):
 *   [0..11]   start_pos (float[3])
 *   [12..23]  goal_pos  (float[3])
 *   [24..27]  strategy  (uint32_t)
 *   [28..31]  agent_radius (float)
 *   [32..35]  agent_height (float)
 *   [36..39]  max_waypoints (uint32_t)
 *
 * Writes waypoint result to result_ptr (pre-allocated by VM).
 */
void npc_nav_world_execute(npc_nav_world_t *nw,
                           const uint8_t params[64],
                           uint8_t *result_ptr,
                           uint32_t result_cap);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_NPC_NAV_WORLD_H */
