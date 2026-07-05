/**
 * @file npc_svo.h
 * @brief Sparse Voxel Octree (SVO) grid for navigation.
 *
 * A sparse octree representation of static level geometry, chunked into
 * sections for cache-friendly traversal and localized updates.
 *
 * Design reference: tickets/rpg-nav02.md
 */

#ifndef FERRUM_NPC_SVO_H
#define FERRUM_NPC_SVO_H

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/physics/aabb.h"
#include "ferrum/physics/mesh_collider.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Constants ──────────────────────────────────────────────────── */

/** @brief Maximum SVO depth (default 8 → ~0.4 m voxels for 100 m world). */
#define NPC_SVO_MAX_DEPTH 13

/** @brief Invalid node / child index. */
#define NPC_SVO_INVALID_NODE 0xFFFFFFFFu

/** @brief Maximum number of sections per grid. */
#define NPC_SVO_MAX_SECTIONS 4096

/** @brief Maximum dynamic blockers per section. */
#define NPC_SVO_MAX_BLOCKERS_PER_SECTION 64

/* ── Flags ──────────────────────────────────────────────────────── */

enum {
    NPC_SVO_FLAG_NONE      = 0,
    NPC_SVO_FLAG_SOLID     = 1 << 0, /**< Contains static geometry. */
    NPC_SVO_FLAG_WALKABLE  = 1 << 1, /**< Walkable floor voxel. */
    NPC_SVO_FLAG_PORTAL    = 1 << 2, /**< Connects to another section. */
};

/* ── Types ──────────────────────────────────────────────────────── */

/**
 * @brief Single SVO node.
 *
 * 8 children indexed [0..7] in Morton order:
 *   child 0 = (0,0,0), child 1 = (1,0,0), child 2 = (0,1,0), child 3 = (1,1,0),
 *   child 4 = (0,0,1), child 5 = (1,0,1), child 6 = (0,1,1), child 7 = (1,1,1).
 *
 * Children are stored as indices into the global node pool.
 * A leaf node has all children == NPC_SVO_INVALID_NODE.
 */
typedef struct npc_svo_node {
    uint32_t children[8]; /**< Child indices or NPC_SVO_INVALID_NODE. */
    uint32_t parent;      /**< Parent index, NPC_SVO_INVALID_NODE for root. */
    uint8_t  occupancy;   /**< Bitmask of occupied child slots. */
    uint8_t  flags;       /**< SOLID | WALKABLE | PORTAL. */
    uint16_t material;    /**< Material ID (0=air, 1=stone, 2=wood, ...). */
} npc_svo_node_t;

_Static_assert(sizeof(npc_svo_node_t) == 40, "npc_svo_node_t must be 40 bytes");

/**
 * @brief A spatial section (chunk) of the SVO.
 *
 * Each section owns a subtree of the global node pool.
 */
typedef struct npc_svo_chunk {
    uint32_t    section_id;  /**< Spatial section index. */
    uint32_t    root_node;   /**< Index into global node pool, or INVALID. */
    phys_aabb_t bounds;      /**< World-space AABB of this chunk. */
} npc_svo_chunk_t;

_Static_assert(sizeof(npc_svo_chunk_t) == 32, "npc_svo_chunk_t must be 32 bytes");

/**
 * @brief Dynamic convex blocker overlaid on the SVO at query time.
 */
typedef struct npc_svo_blocker {
    phys_aabb_t bounds;     /**< World-space AABB of the blocker. */
    uint32_t    section_id; /**< Section this blocker affects (0xFFFFFFFF = all). */
    uint32_t    flags;      /**< Reserved. */
} npc_svo_blocker_t;

_Static_assert(sizeof(npc_svo_blocker_t) == 32, "npc_svo_blocker_t must be 32 bytes");

/**
 * @brief SVO grid — owns all nodes and chunks.
 */
typedef struct npc_svo_grid {
    npc_svo_node_t *nodes;      /**< Global node pool. */
    uint32_t        node_count; /**< Allocated nodes. */
    uint32_t        node_cap;   /**< Capacity of node pool. */

    npc_svo_chunk_t *chunks;      /**< Section chunks. */
    uint32_t         chunk_count; /**< Allocated chunks. */
    uint32_t         chunk_cap;   /**< Capacity of chunk array. */

    float    voxel_size; /**< Meters per smallest voxel (world size / 2^depth). */
    uint32_t max_depth;  /**< Maximum tree depth (e.g. 8). */

    phys_aabb_t world_bounds; /**< Overall world AABB. */
} npc_svo_grid_t;

/* ── Lifecycle ──────────────────────────────────────────────────── */

/**
 * @brief Initialize an empty SVO grid.
 *
 * @param grid      Grid to initialize (must not be NULL).
 * @param bounds    World-space bounds covered by the grid.
 * @param max_depth Maximum octree depth (≤ NPC_SVO_MAX_DEPTH).
 * @return true on success, false on allocation failure.
 */
bool npc_svo_grid_init(npc_svo_grid_t *grid, phys_aabb_t bounds, uint32_t max_depth);

/**
 * @brief Destroy an SVO grid and free all internal memory.
 *
 * @param grid Grid to destroy (must not be NULL). Safe to call on a zeroed grid.
 */
void npc_svo_grid_destroy(npc_svo_grid_t *grid);

/**
 * @brief Allocate a new node from the global pool.
 *
 * @param grid Grid (must be initialized).
 * @return Node index, or NPC_SVO_INVALID_NODE on pool exhaustion.
 */
uint32_t npc_svo_alloc_node(npc_svo_grid_t *grid);

/**
 * @brief Clear all nodes and chunks, returning the grid to an empty state
 *        while preserving the world bounds and depth configuration.
 *
 * @param grid Grid to clear (must be initialized).
 */
void npc_svo_grid_clear(npc_svo_grid_t *grid);

/* ── Construction ───────────────────────────────────────────────── */

/**
 * @brief Rasterize a single triangle into the SVO, marking intersected
 *        leaf voxels as SOLID.
 *
 * @param grid SVO grid.
 * @param tri  Triangle vertices (world space).
 */
void npc_svo_rasterize_triangle(npc_svo_grid_t *grid,
                                const phys_triangle_t *tri);

/**
 * @brief Rasterize an entire mesh into the SVO.
 *
 * @param grid      SVO grid.
 * @param triangles Array of triangles (world space).
 * @param tri_count Number of triangles.
 */
void npc_svo_rasterize_mesh(npc_svo_grid_t *grid,
                            const phys_triangle_t *triangles,
                            uint32_t tri_count);

/* ── Flood-fill ─────────────────────────────────────────────────── */

/**
 * @brief Flood-fill walkable voxels from a seed position.
 *
 * Starting from the voxel containing @p seed_pos, flood outward through
 * empty voxels that have SOLID directly beneath them (within agent_height)
 * and enough clearance above (agent_height). Mark reached voxels WALKABLE.
 *
 * @param grid          SVO grid.
 * @param seed_pos      World-space seed position.
 * @param agent_height  Minimum vertical clearance (meters).
 * @param agent_radius  Minimum horizontal clearance (meters).
 * @param truncated     If non-NULL, set to true when the BFS queue overflowed
 *                      and the floodfill may be incomplete.
 * @return Number of voxels marked walkable.
 */
uint32_t npc_svo_floodfill_walkable(npc_svo_grid_t *grid,
                                    phys_vec3_t seed_pos,
                                    float agent_height,
                                    float agent_radius,
                                    bool *truncated);

/* ── Blocker overlay ────────────────────────────────────────────── */

/**
 * @brief Test whether a world-space AABB intersects any dynamic blocker.
 *
 * @param grid           SVO grid.
 * @param blockers       Active blocker array.
 * @param blocker_count  Number of active blockers.
 * @param world_aabb     AABB to test.
 * @param section_id_hint If known, restrict to blockers in this section.
 * @return true if the AABB is blocked by at least one blocker.
 */
bool npc_svo_aabb_blocked(const npc_svo_grid_t *grid,
                          const npc_svo_blocker_t *blockers,
                          uint32_t blocker_count,
                          phys_aabb_t world_aabb,
                          uint32_t section_id_hint);

/**
 * @brief Check if a specific voxel is inside any dynamic blocker.
 *
 * Used during A* expansion to reject nodes that intersect dynamic
 * obstacles without mutating persistent SVO state.
 *
 * @param grid           SVO grid.
 * @param blockers       Active blocker array.
 * @param blocker_count  Number of active blockers.
 * @param vx             Voxel X coordinate at max depth.
 * @param vy             Voxel Y coordinate at max depth.
 * @param vz             Voxel Z coordinate at max depth.
 * @return true if the voxel intersects at least one blocker.
 */
bool npc_svo_voxel_blocked(const npc_svo_grid_t *grid,
                           const npc_svo_blocker_t *blockers,
                           uint32_t blocker_count,
                           uint32_t vx, uint32_t vy, uint32_t vz);

/* ── Query ──────────────────────────────────────────────────────── */

/**
 * @brief Find the leaf node containing a world-space position.
 *
 * @param grid      SVO grid.
 * @param position  World-space position.
 * @param out_node  Optional: receives the leaf node index.
 * @return Flags of the leaf node (0 if outside grid).
 */
uint8_t npc_svo_query_point(const npc_svo_grid_t *grid,
                            phys_vec3_t position,
                            uint32_t *out_node);

/**
 * @brief Compute the world-space AABB of a single voxel at a given
 *        tree depth and morton coordinates.
 *
 * @param grid  SVO grid.
 * @param depth Tree depth (0 = root).
 * @param x     Voxel X coordinate at this depth.
 * @param y     Voxel Y coordinate at this depth.
 * @param z     Voxel Z coordinate at this depth.
 * @return World-space AABB of the voxel.
 */
phys_aabb_t npc_svo_voxel_aabb(const npc_svo_grid_t *grid,
                               uint32_t depth,
                               uint32_t x, uint32_t y, uint32_t z);

/**
 * @brief Convert world-space position to voxel coordinates at max depth.
 *
 * @param grid     SVO grid.
 * @param position World-space position.
 * @param out_x    Receives voxel X index.
 * @param out_y    Receives voxel Y index.
 * @param out_z    Receives voxel Z index.
 * @return true if position is inside the grid bounds.
 */
bool npc_svo_world_to_voxel(const npc_svo_grid_t *grid,
                            phys_vec3_t position,
                            uint32_t *out_x,
                            uint32_t *out_y,
                            uint32_t *out_z);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_NPC_SVO_H */
