/**
 * @file npc_svo_init.c
 * @brief SVO grid lifecycle: init, destroy, alloc, clear.
 *
 * Non-static functions (4 of 4 max):
 *   1. npc_svo_grid_init
 *   2. npc_svo_grid_destroy
 *   3. npc_svo_alloc_node
 *   4. npc_svo_grid_clear
 */

#include "ferrum/npc/npc_svo.h"

#include <stdlib.h>
#include <string.h>

/* ── Internal constants ─────────────────────────────────────────── */

/** Initial node pool capacity. */
#define NODE_POOL_INITIAL 65536

/** Initial chunk array capacity. */
#define CHUNK_POOL_INITIAL 64

/* ── Public API ─────────────────────────────────────────────────── */

bool npc_svo_grid_init(npc_svo_grid_t *grid, phys_aabb_t bounds,
                       uint32_t max_depth) {
    if (!grid || max_depth == 0 || max_depth > NPC_SVO_MAX_DEPTH) {
        return false;
    }

    memset(grid, 0, sizeof(*grid));
    grid->world_bounds = bounds;
    grid->max_depth    = max_depth;

    /* Compute voxel size: world extents / 2^depth. */
    float ex = bounds.max.x - bounds.min.x;
    float ey = bounds.max.y - bounds.min.y;
    float ez = bounds.max.z - bounds.min.z;
    uint32_t cells   = 1u << max_depth;
    float    max_ext = ex;
    if (ey > max_ext) max_ext = ey;
    if (ez > max_ext) max_ext = ez;
    grid->voxel_size = max_ext / (float)cells;

    grid->nodes = (npc_svo_node_t *)calloc(NODE_POOL_INITIAL,
                                            sizeof(npc_svo_node_t));
    if (!grid->nodes) return false;
    /* Initialize root node: all child slots invalid, no flags. */
    memset(grid->nodes, 0xFF, sizeof(npc_svo_node_t));
    grid->nodes[0].flags = 0;
    /* Reserve index 0 for the root: alloc_node starts from index 1. */
    grid->node_count = 1;
    grid->node_cap = NODE_POOL_INITIAL;

    grid->chunks = (npc_svo_chunk_t *)calloc(CHUNK_POOL_INITIAL,
                                             sizeof(npc_svo_chunk_t));
    if (!grid->chunks) {
        free(grid->nodes);
        grid->nodes = NULL;
        return false;
    }
    grid->chunk_cap = CHUNK_POOL_INITIAL;

    /* Node 0 is reserved as a "null" node (all children invalid). */
    memset(&grid->nodes[0], 0xFF, sizeof(npc_svo_node_t));
    grid->nodes[0].flags = 0;
    grid->node_count = 1;

    return true;
}

void npc_svo_grid_destroy(npc_svo_grid_t *grid) {
    if (!grid) return;
    free(grid->nodes);
    free(grid->chunks);
    memset(grid, 0, sizeof(*grid));
}

uint32_t npc_svo_alloc_node(npc_svo_grid_t *grid) {
    if (!grid) return NPC_SVO_INVALID_NODE;

    if (grid->node_count >= grid->node_cap) {
        uint32_t new_cap = grid->node_cap * 2;
        if (new_cap < grid->node_cap) return NPC_SVO_INVALID_NODE; /* overflow */
        npc_svo_node_t *new_nodes = (npc_svo_node_t *)realloc(
            grid->nodes, new_cap * sizeof(npc_svo_node_t));
        if (!new_nodes) return NPC_SVO_INVALID_NODE;
        grid->nodes    = new_nodes;
        grid->node_cap = new_cap;
    }

    uint32_t idx = grid->node_count++;
    memset(&grid->nodes[idx], 0xFF, sizeof(npc_svo_node_t));
    grid->nodes[idx].flags = 0;
    return idx;
}

void npc_svo_grid_clear(npc_svo_grid_t *grid) {
    if (!grid) return;

    /* Reset node pool, preserving node 0 as null. */
    grid->node_count = 1;
    memset(&grid->nodes[0], 0xFF, sizeof(npc_svo_node_t));

    /* Clear chunks without freeing. */
    grid->chunk_count = 0;
}
