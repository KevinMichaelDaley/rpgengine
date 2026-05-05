/**
 * @file npc_svo_floodfill.c
 * @brief Walkable flood-fill from seed positions.
 *
 * Marks empty voxels with solid floor beneath and enough headroom
 * above as WALKABLE. Uses 6-connected breadth-first expansion.
 *
 * Non-static functions (1 of 4 max):
 *   1. npc_svo_floodfill_walkable
 *
 * Static helpers:
 *   - leaf_coords_    — recover voxel coordinates from a leaf node index
 *   - enqueue_        — add neighbor to BFS queue
 *   - has_floor_      — check if voxel has solid directly below within height
 *   - has_headroom_   — check if voxel has empty space above within radius
 */

#include "ferrum/npc/npc_svo.h"

#include <stdlib.h>
#include <string.h>

/* ── Static helpers ─────────────────────────────────────────────── */

static inline bool aabb_contains_point_(phys_aabb_t aabb, phys_vec3_t p) {
    return p.x >= aabb.min.x && p.x <= aabb.max.x &&
           p.y >= aabb.min.y && p.y <= aabb.max.y &&
           p.z >= aabb.min.z && p.z <= aabb.max.z;
}

/**
 * @brief Query the SVO at a specific voxel coordinate (max depth).
 *
 * @return flags of the leaf node, or NPC_SVO_FLAG_SOLID if out of bounds.
 */
static uint8_t query_voxel_(const npc_svo_grid_t *grid,
                            uint32_t vx, uint32_t vy, uint32_t vz) {
    uint32_t node_idx = 0;
    uint32_t cells    = 1u << grid->max_depth;

    for (uint32_t d = 0; d < grid->max_depth; d++) {
        cells >>= 1;
        uint32_t cx = (vx / cells) & 1;
        uint32_t cy = (vy / cells) & 1;
        uint32_t cz = (vz / cells) & 1;
        uint32_t child_idx = (cz << 2) | (cy << 1) | cx;
        uint32_t child = grid->nodes[node_idx].children[child_idx];
        if (child == NPC_SVO_INVALID_NODE) {
            return 0; /* empty space */
        }
        node_idx = child;
    }
    return grid->nodes[node_idx].flags;
}

/**
 * @brief Check if the voxel at (vx,vy,vz) has a continuous solid floor
 *        spanning agent_height/voxel_size voxels below it.
 */
static bool has_floor_(const npc_svo_grid_t *grid,
                       uint32_t vx, uint32_t vy, uint32_t vz,
                       float agent_height) {
    uint32_t needed = (uint32_t)(agent_height / grid->voxel_size);
    if (needed < 1) needed = 1;
    for (uint32_t i = 1; i <= needed; i++) {
        if (vz < i) return false;
        uint8_t f = query_voxel_(grid, vx, vy, vz - i);
        if ((f & NPC_SVO_FLAG_SOLID) == 0) return false;
    }
    return true;
}

/**
 * @brief Check if the voxel at (vx,vy,vz) has enough empty headroom
 *        spanning agent_height/voxel_size voxels above it.
 */
static bool has_headroom_(const npc_svo_grid_t *grid,
                          uint32_t vx, uint32_t vy, uint32_t vz,
                          float agent_height) {
    uint32_t needed = (uint32_t)(agent_height / grid->voxel_size);
    uint32_t cells = 1u << grid->max_depth;
    for (uint32_t i = 1; i <= needed; i++) {
        if (vz + i >= cells) return false;
        uint8_t f = query_voxel_(grid, vx, vy, vz + i);
        if (f & NPC_SVO_FLAG_SOLID) return false;
    }
    return true;
}

/* ── Public API ─────────────────────────────────────────────────── */

uint32_t npc_svo_floodfill_walkable(npc_svo_grid_t *grid,
                                    phys_vec3_t seed_pos,
                                    float agent_height,
                                    float agent_radius,
                                    bool *truncated) {
    (void)agent_radius;
    if (truncated) *truncated = false;
    if (!grid || grid->max_depth == 0) return 0;

    uint32_t cells = 1u << grid->max_depth;
    uint32_t sx, sy, sz;
    if (!npc_svo_world_to_voxel(grid, seed_pos, &sx, &sy, &sz)) {
        return 0;
    }

    uint32_t queue_cap = cells * cells;
    if (queue_cap < 1024) queue_cap = 1024;

    uint32_t *queue = (uint32_t *)malloc(queue_cap * sizeof(uint32_t));
    if (!queue) {
        queue_cap = 4096;
        queue = (uint32_t *)malloc(queue_cap * sizeof(uint32_t));
        if (!queue) return 0;
    }

    uint8_t *visited = (uint8_t *)calloc(cells * cells * cells, sizeof(uint8_t));
    if (!visited) {
        free(queue);
        return 0;
    }

    uint32_t qhead = 0, qtail = 0;
    bool overflowed = false;

#define PUSH(vx, vy, vz)                                                      \
    do {                                                                       \
        if ((vx) < cells && (vy) < cells && (vz) < cells) {                    \
            uint32_t idx_ = (vz) * cells * cells + (vy) * cells + (vx);        \
            if (!visited[idx_]) {                                              \
                if (qtail < queue_cap) {                                       \
                    visited[idx_] = 1;                                         \
                    queue[qtail++] = ((vx) << 20) | ((vy) << 10) | (vz);       \
                } else {                                                       \
                    overflowed = true;                                         \
                }                                                              \
            }                                                                  \
        }                                                                      \
    } while (0)

#define VX(q) (((q) >> 20) & 0x3FF)
#define VY(q) (((q) >> 10) & 0x3FF)
#define VZ(q) ((q) & 0x3FF)

    /* Start from seed. */
    {
        uint32_t idx = sz * cells * cells + sy * cells + sx;
        visited[idx] = 1;
        queue[qtail++] = (sx << 20) | (sy << 10) | sz;
    }

    uint32_t marked = 0;

    while (qhead < qtail) {
        uint32_t packed = queue[qhead++];
        uint32_t vx = VX(packed);
        uint32_t vy = VY(packed);
        uint32_t vz = VZ(packed);

        uint8_t flags = query_voxel_(grid, vx, vy, vz);
        if (flags & NPC_SVO_FLAG_SOLID) continue; /* inside geometry */

        if (!has_floor_(grid, vx, vy, vz, agent_height)) continue;
        if (!has_headroom_(grid, vx, vy, vz, agent_height)) continue;

        /* Mark walkable. */
        {
            uint32_t leaf = 0;
            uint32_t c = cells;
            for (uint32_t d = 0; d < grid->max_depth; d++) {
                c >>= 1;
                uint32_t cx = (vx / c) & 1;
                uint32_t cy = (vy / c) & 1;
                uint32_t cz = (vz / c) & 1;
                uint32_t ci = (cz << 2) | (cy << 1) | cx;
                uint32_t child = grid->nodes[leaf].children[ci];
                if (child == NPC_SVO_INVALID_NODE) break;
                leaf = child;
            }
            if (leaf != 0) {
                grid->nodes[leaf].flags |= NPC_SVO_FLAG_WALKABLE;
                marked++;
            }
        }

        /* Enqueue 6-connected neighbors. */
        if (vx > 0) PUSH(vx - 1, vy, vz);
        if (vx + 1 < cells) PUSH(vx + 1, vy, vz);
        if (vy > 0) PUSH(vx, vy - 1, vz);
        if (vy + 1 < cells) PUSH(vx, vy + 1, vz);
        if (vz > 0) PUSH(vx, vy, vz - 1);
        if (vz + 1 < cells) PUSH(vx, vy, vz + 1);
        if (overflowed) break;
    }

#undef PUSH
#undef VX
#undef VY
#undef VZ

    if (truncated) *truncated = overflowed;
    free(queue);
    free(visited);
    return marked;
}
