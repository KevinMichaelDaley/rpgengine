/**
 * @file npc_nav_graph.h
 * @brief Navigation graph types: chunk graph nodes/edges, hierarchical graph.
 *
 * The chunk graph (L1) is extracted from the SVO: each connected walkable
 * component becomes a node.  Portal edges connect components in adjacent
 * sections.  Higher levels (L2+) are produced by recursive partitioning.
 *
 * This module serves both pathfinding (rpg-nav04) and audio propagation
 * (rpg-llm04).
 */

#ifndef FERRUM_NPC_NAV_GRAPH_H
#define FERRUM_NPC_NAV_GRAPH_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/npc/npc_svo.h"
#include "ferrum/physics/phys_vec3.h"

/* ── Edge flags ──────────────────────────────────────────────────── */

enum {
    NPC_NAV_EDGE_NONE   = 0,
    NPC_NAV_EDGE_PORTAL = 1 << 0, /**< Connects across a section boundary. */
    NPC_NAV_EDGE_STAIRS = 1 << 1, /**< Vertical connector (stairs/ladder). */
};

/* ── Chunk graph (L1) ────────────────────────────────────────────── */

/**
 * @brief A node in the chunk graph — represents a connected walkable region.
 */
typedef struct npc_nav_graph_node {
    uint32_t    node_id;     /**< Index in the graph's node array. */
    uint32_t    section_id;  /**< SVO section this node belongs to. */
    phys_aabb_t bounds;      /**< World-space bounding box of the region. */
    phys_vec3_t centroid;    /**< Center-of-mass of walkable voxels. */
    float       radius;      /**< Approximate radius from centroid (meters). */

    uint32_t               edge_count;
    uint32_t               edge_cap;
    struct npc_nav_graph_edge *edges;
} npc_nav_graph_node_t;

/**
 * @brief A directed edge between two chunk-graph nodes.
 */
typedef struct npc_nav_graph_edge {
    uint32_t to_node_id;    /**< Index of target node. */
    float    cost;          /**< Euclidean distance between centroids. */
    uint32_t flags;         /**< NPC_NAV_EDGE_* bitmask. */
} npc_nav_graph_edge_t;

/**
 * @brief Complete chunk graph extracted from the SVO.
 */
typedef struct npc_nav_graph {
    npc_nav_graph_node_t *nodes;
    uint32_t               node_count;
    uint32_t               node_cap;

    /** Edge storage: all edges are stored in node-local arrays.
     *  edge_cap is a shared watermark (all nodes grow independently). */
    uint32_t               edge_cap;
} npc_nav_graph_t;

/* ── Hierarchical graph (L2+) ────────────────────────────────────── */

/**
 * @brief An undirected edge between two hierarchical nodes.
 */
typedef struct npc_nav_hedge {
    uint32_t to_node_id;
    float    cost;
    uint32_t flags;
} npc_nav_hedge_t;

/**
 * @brief A node at a hierarchical level (L0+).
 */
typedef struct npc_nav_hnode {
    uint32_t    level;           /**< 0 = L1 (chunk), 1 = L2, etc. */
    uint32_t    child_start;     /**< First child index in level-1 array. */
    uint32_t    child_count;     /**< Number of children. */
    phys_aabb_t bounds;          /**< Union of child bounds. */
    phys_vec3_t centroid;        /**< Average of child centroids. */

    uint32_t              edge_count;
    uint32_t              edge_cap;
    struct npc_nav_hedge *edges;
} npc_nav_hnode_t;

/**
 * @brief Multi-level hierarchical navigation graph.
 *
 * Level 0 mirrors the chunk graph (L1).  Each level N+1 partitions
 * level N into clusters.
 */
typedef struct npc_nav_hgraph {
    npc_nav_hnode_t *nodes_per_level[8]; /**< One array per level. */
    uint32_t          node_count_per_level[8];
    uint32_t          level_count;
    uint32_t          max_levels;
} npc_nav_hgraph_t;

/* ── Chunk graph lifecycle ───────────────────────────────────────── */

/**
 * @brief Initialize an empty chunk graph.
 *
 * @return true on success, false on allocation failure.
 */
bool npc_nav_graph_init(npc_nav_graph_t *graph, uint32_t node_cap,
                        uint32_t edge_cap);

/**
 * @brief Destroy a chunk graph and free all storage.
 */
void npc_nav_graph_destroy(npc_nav_graph_t *graph);

/**
 * @brief Add a node to the graph.
 *
 * @return The new node ID, or UINT32_MAX on failure.
 */
uint32_t npc_nav_graph_add_node(npc_nav_graph_t *graph,
                                uint32_t section_id,
                                phys_vec3_t centroid,
                                phys_aabb_t bounds,
                                float radius);

/**
 * @brief Add a directed edge from @p from to @p to.
 *
 * @return true on success, false if nodes invalid or allocation fails.
 */
bool npc_nav_graph_add_edge(npc_nav_graph_t *graph,
                            uint32_t from, uint32_t to,
                            float cost, uint32_t flags);

/* ── Chunk graph extraction from SVO ─────────────────────────────── */

/**
 * @brief Extract connected walkable components from an SVO grid into
 *        a navigation chunk graph.
 *
 * Each 6-connected region of WALKABLE voxels becomes one graph node.
 * Nodes that touch (adjacent walkable voxels) get edges.
 *
 * @param graph      Target graph (must be initialized).
 * @param svo        Source SVO grid.
 * @param blockers   Dynamic blockers (may be NULL).
 * @param blocker_count Number of blockers.
 * @return Number of nodes created.
 */
uint32_t npc_nav_graph_extract(npc_nav_graph_t *graph,
                               const npc_svo_grid_t *svo,
                               const npc_svo_blocker_t *blockers,
                               uint32_t blocker_count);

/* ── Hierarchical graph lifecycle ────────────────────────────────── */

/**
 * @brief Initialize an empty hierarchical graph.
 */
bool npc_nav_hgraph_init(npc_nav_hgraph_t *hg, uint32_t max_levels);

/**
 * @brief Destroy a hierarchical graph.
 */
void npc_nav_hgraph_destroy(npc_nav_hgraph_t *hg);

/**
 * @brief Build hierarchical levels from a chunk graph.
 *
 * Level 0 is a 1:1 copy of @p graph.  Each subsequent level clusters
 * nodes from the previous level, stopping when the top level has fewer
 * than 64 nodes or @p max_levels is reached.
 *
 * Portal edges (NPC_NAV_EDGE_PORTAL) are preserved across all levels.
 *
 * @param hg         Target hierarchical graph (initialized).
 * @param graph      Source chunk graph (L1).
 * @param max_levels Maximum depth (≤ hg->max_levels).
 * @return true on success.
 */
bool npc_nav_hgraph_reduce(npc_nav_hgraph_t *hg,
                           const npc_nav_graph_t *graph,
                           uint32_t max_levels);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_NPC_NAV_GRAPH_H */
