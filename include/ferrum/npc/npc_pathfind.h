/**
 * @file npc_pathfind.h
 * @brief Pathfinding API: SVO A*, graph A*, hierarchical A*, LOS shortcut.
 */

#ifndef FERRUM_NPC_PATHFIND_H
#define FERRUM_NPC_PATHFIND_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/physics/phys_vec3.h"

struct npc_svo_grid;
struct npc_svo_blocker;
struct npc_nav_graph;

/* ── Path strategies ─────────────────────────────────────────────── */

typedef enum {
    NPC_PATH_SVO_ONLY,
    NPC_PATH_CHUNK_GRAPH,
    NPC_PATH_HIERARCHICAL,
    NPC_PATH_NAVMESH,
} npc_path_strategy_t;

/* ── Path request / result ───────────────────────────────────────── */

typedef struct npc_hpath_request {
    phys_vec3_t start_pos;
    phys_vec3_t goal_pos;
    uint32_t    start_section;
    uint32_t    goal_section;
    float       agent_radius;
    float       agent_height;
} npc_hpath_request_t;

typedef struct npc_hpath_result {
    uint32_t    waypoint_count;
    phys_vec3_t *waypoints;
    float       total_cost;
    bool        partial;
} npc_hpath_result_t;

/* ── SVO A* (ground level voxel pathfinding) ──────────────────────── */

bool npc_svo_astar(const struct npc_svo_grid *svo,
                   const struct npc_svo_blocker *blockers,
                   uint32_t blocker_count,
                   phys_vec3_t start,
                   phys_vec3_t goal,
                   phys_vec3_t *out_waypoints,
                   uint32_t *out_count,
                   uint32_t max_waypoints,
                   float agent_radius,
                   float agent_height);

/* ── Graph A* (on chunk graph nodes) ──────────────────────────────── */

bool npc_graph_astar(const struct npc_nav_graph *graph,
                     uint32_t start_node,
                     uint32_t goal_node,
                     uint32_t *out_nodes,
                     uint32_t *out_count,
                     uint32_t max_nodes);

/* ── LOS shortcutting ────────────────────────────────────────────── */

void npc_pathfind_shortcut(const phys_vec3_t *in, uint32_t in_count,
                           phys_vec3_t *out, uint32_t *out_count,
                           uint32_t max_out,
                           const struct npc_svo_grid *svo,
                           const struct npc_svo_blocker *blockers,
                           uint32_t blocker_count);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_NPC_PATHFIND_H */
