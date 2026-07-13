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

/* Reset one node to the empty state: all child slots invalid, no flags, and a
 * zeroed shading pyramid (the 0xFF fill would otherwise leave the float colour
 * fields as NaN, poisoning the lightmap far-field gather). */
static void npc_svo_node_reset(npc_svo_node_t *node) {
    memset(node, 0xFF, sizeof(*node)); /* child slots + parent -> INVALID */
    node->flags = 0;
    node->occupancy = 0; /* start empty so rasterize's |= bitmask is meaningful */
    node->diffuse[0] = node->diffuse[1] = node->diffuse[2] = 0.0f;
    node->emissive[0] = node->emissive[1] = node->emissive[2] = 0.0f;
}

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
    npc_svo_node_reset(&grid->nodes[0]);
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
    npc_svo_node_reset(&grid->nodes[0]);
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
    npc_svo_node_reset(&grid->nodes[idx]);
    return idx;
}

void npc_svo_grid_clear(npc_svo_grid_t *grid) {
    if (!grid) return;

    /* Reset node pool, preserving node 0 as null. */
    grid->node_count = 1;
    npc_svo_node_reset(&grid->nodes[0]);

    /* Clear chunks without freeing. */
    grid->chunk_count = 0;
}
