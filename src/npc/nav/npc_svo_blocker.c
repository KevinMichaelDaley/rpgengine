/**
 * @file npc_svo_blocker.c
 * @brief Dynamic convex blocker overlay + point query on the SVO.
 *
 * Blockers are AABBs that act as dynamic obstacles for the pathfinding
 * agent. They do NOT mutate persistent SVO node flags. Instead, the
 * pathfinder queries blocker intersection at expansion time using the
 * agent's bounding volume at the candidate voxel.
 *
 * Non-static functions (4 of 4 max):
 *   1. npc_svo_aabb_blocked
 *   2. npc_svo_voxel_blocked
 *   3. npc_svo_voxel_aabb
 *   4. npc_svo_world_to_voxel
 *   (npc_svo_query_point moved here from planned separate file)
 */

#include "ferrum/npc/npc_svo.h"

#include <string.h>

/* ── Public API ─────────────────────────────────────────────────── */

bool npc_svo_aabb_blocked(const npc_svo_grid_t *grid,
                          const npc_svo_blocker_t *blockers,
                          uint32_t blocker_count,
                          phys_aabb_t world_aabb,
                          uint32_t section_id_hint) {
    (void)grid;
    if (!blockers || blocker_count == 0) return false;

    for (uint32_t i = 0; i < blocker_count; i++) {
        const npc_svo_blocker_t *b = &blockers[i];
        if (section_id_hint != 0xFFFFFFFFu &&
            b->section_id != 0xFFFFFFFFu &&
            b->section_id != section_id_hint) {
            continue;
        }

        /* Quick AABB-AABB reject. */
        if (world_aabb.max.x < b->bounds.min.x || world_aabb.min.x > b->bounds.max.x ||
            world_aabb.max.y < b->bounds.min.y || world_aabb.min.y > b->bounds.max.y ||
            world_aabb.max.z < b->bounds.min.z || world_aabb.min.z > b->bounds.max.z) {
            continue;
        }
        return true;
    }
    return false;
}

bool npc_svo_voxel_blocked(const npc_svo_grid_t *grid,
                           const npc_svo_blocker_t *blockers,
                           uint32_t blocker_count,
                           uint32_t vx, uint32_t vy, uint32_t vz) {
    if (!grid || !blockers || blocker_count == 0) return false;

    phys_aabb_t voxel_aabb = npc_svo_voxel_aabb(grid, grid->max_depth, vx, vy, vz);
    return npc_svo_aabb_blocked(grid, blockers, blocker_count, voxel_aabb, 0xFFFFFFFFu);
}

phys_aabb_t npc_svo_voxel_aabb(const npc_svo_grid_t *grid,
                               uint32_t depth,
                               uint32_t x, uint32_t y, uint32_t z) {
    phys_aabb_t result = {0};
    if (!grid || depth > grid->max_depth) return result;

    uint32_t cells = 1u << depth;
    float    sx    = grid->world_bounds.max.x - grid->world_bounds.min.x;
    float    sy    = grid->world_bounds.max.y - grid->world_bounds.min.y;
    float    sz    = grid->world_bounds.max.z - grid->world_bounds.min.z;

    float cell_w = sx / (float)cells;
    float cell_h = sy / (float)cells;
    float cell_d = sz / (float)cells;

    result.min.x = grid->world_bounds.min.x + (float)x * cell_w;
    result.min.y = grid->world_bounds.min.y + (float)y * cell_h;
    result.min.z = grid->world_bounds.min.z + (float)z * cell_d;
    result.max.x = result.min.x + cell_w;
    result.max.y = result.min.y + cell_h;
    result.max.z = result.min.z + cell_d;

    return result;
}

uint8_t npc_svo_query_point(const npc_svo_grid_t *grid,
                            phys_vec3_t position,
                            uint32_t *out_node) {
    if (!grid) {
        if (out_node) *out_node = NPC_SVO_INVALID_NODE;
        return 0;
    }

    if (position.x < grid->world_bounds.min.x ||
        position.x > grid->world_bounds.max.x ||
        position.y < grid->world_bounds.min.y ||
        position.y > grid->world_bounds.max.y ||
        position.z < grid->world_bounds.min.z ||
        position.z > grid->world_bounds.max.z) {
        if (out_node) *out_node = NPC_SVO_INVALID_NODE;
        return 0;
    }

    uint32_t node_idx = 0;
    uint32_t cells = 1u << grid->max_depth;
    uint32_t vx = (uint32_t)((position.x - grid->world_bounds.min.x) /
                             (grid->world_bounds.max.x - grid->world_bounds.min.x) * (float)cells);
    uint32_t vy = (uint32_t)((position.y - grid->world_bounds.min.y) /
                             (grid->world_bounds.max.y - grid->world_bounds.min.y) * (float)cells);
    uint32_t vz = (uint32_t)((position.z - grid->world_bounds.min.z) /
                             (grid->world_bounds.max.z - grid->world_bounds.min.z) * (float)cells);
    if (vx >= cells) vx = cells - 1;
    if (vy >= cells) vy = cells - 1;
    if (vz >= cells) vz = cells - 1;

    for (uint32_t d = 0; d < grid->max_depth; d++) {
        cells >>= 1;
        uint32_t cx = (vx / cells) & 1;
        uint32_t cy = (vy / cells) & 1;
        uint32_t cz = (vz / cells) & 1;
        uint32_t ci = (cz << 2) | (cy << 1) | cx;
        uint32_t child = grid->nodes[node_idx].children[ci];
        if (child == NPC_SVO_INVALID_NODE) {
            if (out_node) *out_node = node_idx;
            return grid->nodes[node_idx].flags;
        }
        node_idx = child;
    }

    if (out_node) *out_node = node_idx;
    return grid->nodes[node_idx].flags;
}

bool npc_svo_world_to_voxel(const npc_svo_grid_t *grid,
                            phys_vec3_t position,
                            uint32_t *out_x,
                            uint32_t *out_y,
                            uint32_t *out_z) {
    if (!grid || !out_x || !out_y || !out_z) return false;

    if (position.x < grid->world_bounds.min.x ||
        position.x > grid->world_bounds.max.x ||
        position.y < grid->world_bounds.min.y ||
        position.y > grid->world_bounds.max.y ||
        position.z < grid->world_bounds.min.z ||
        position.z > grid->world_bounds.max.z) {
        return false;
    }

    uint32_t cells = 1u << grid->max_depth;
    float    sx    = grid->world_bounds.max.x - grid->world_bounds.min.x;
    float    sy    = grid->world_bounds.max.y - grid->world_bounds.min.y;
    float    sz    = grid->world_bounds.max.z - grid->world_bounds.min.z;

    *out_x = (uint32_t)((position.x - grid->world_bounds.min.x) / sx * (float)cells);
    *out_y = (uint32_t)((position.y - grid->world_bounds.min.y) / sy * (float)cells);
    *out_z = (uint32_t)((position.z - grid->world_bounds.min.z) / sz * (float)cells);

    if (*out_x >= cells) *out_x = cells - 1;
    if (*out_y >= cells) *out_y = cells - 1;
    if (*out_z >= cells) *out_z = cells - 1;

    return true;
}
