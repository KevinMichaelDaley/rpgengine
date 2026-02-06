#ifndef FERRUM_PHYSICS_SPATIAL_GRID_H
#define FERRUM_PHYSICS_SPATIAL_GRID_H

/** @file
 * @brief Spatial hash grid for broadphase collision detection.
 *
 * Bodies are inserted based on their AABB; queries return all body indices
 * whose AABBs overlap the query region.  All per-frame memory comes from
 * a phys_frame_arena, so there are no individual frees — the arena is
 * reset once per tick.
 *
 * All public functions are NULL-safe: passing NULL for pointer arguments
 * is a no-op or returns a zero/false default.
 */

#include "ferrum/physics/aabb.h"

#include <stdbool.h>
#include <stdint.h>

struct phys_frame_arena;

#ifdef __cplusplus
extern "C" {
#endif

/* ── Grid cell ──────────────────────────────────────────────────── */

/**
 * @brief A single cell in the spatial hash grid.
 *
 * Stores an arena-allocated array of body indices that hash to this cell.
 * Ownership: the cell does not own its memory — it belongs to the arena.
 */
typedef struct phys_grid_cell {
    uint32_t *body_indices; /**< Arena-allocated index array (may be NULL). */
    uint32_t count;         /**< Number of indices currently stored. */
    uint32_t capacity;      /**< Allocated slots in body_indices. */
} phys_grid_cell_t;

/* ── Spatial grid ───────────────────────────────────────────────── */

/**
 * @brief Spatial hash grid for broadphase collision detection.
 *
 * Ownership: the grid's cell array is arena-allocated and is invalidated
 * when the arena is reset or destroyed.  The grid does not own the arena.
 *
 * Nullability: all public functions tolerate a NULL grid pointer.
 */
typedef struct phys_spatial_grid {
    phys_grid_cell_t *cells;       /**< Hash table of cells (power-of-2 count). */
    uint32_t          cell_count;  /**< Number of cells (power of 2). */
    uint32_t          cell_mask;   /**< cell_count - 1, for fast modulo. */
    float             cell_size;   /**< World-space size of each cell. */
    float             inv_cell_size; /**< 1.0f / cell_size, cached. */
    struct phys_frame_arena *arena;  /**< Arena for cell allocations (not owned). */
} phys_spatial_grid_t;

/* ── Public API ─────────────────────────────────────────────────── */

/**
 * @brief Initialize a spatial hash grid.
 *
 * Allocates the cell array from @p arena and zeroes all cells.
 *
 * @param grid       Grid to initialize (if NULL, no-op).
 * @param cell_count Number of hash buckets (must be a power of 2 and > 0).
 * @param cell_size  World-space size of each cell (must be > 0).
 * @param arena      Frame arena for allocations (must be non-NULL).
 *
 * Side effects: allocates from @p arena.
 */
void phys_spatial_grid_init(phys_spatial_grid_t *grid, uint32_t cell_count,
                            float cell_size, struct phys_frame_arena *arena);

/**
 * @brief Clear all cells in the grid (O(cell_count)).
 *
 * Resets every cell's count to zero.  Does not free arena memory —
 * old allocations are simply abandoned until the arena is reset.
 *
 * @param grid  Grid to clear (if NULL, no-op).
 *
 * Side effects: none beyond zeroing cell counts and pointers.
 */
void phys_spatial_grid_clear(phys_spatial_grid_t *grid);

/**
 * @brief Insert a body into the grid based on its AABB.
 *
 * The body is added to every cell that its AABB overlaps.  Duplicate
 * insertions into the same cell (from multiple AABB sub-regions hashing
 * to the same bucket) are suppressed.
 *
 * @param grid       Grid (if NULL, no-op).
 * @param body_index Index of the body to insert.
 * @param aabb       Axis-aligned bounding box of the body (if NULL, no-op).
 *
 * Side effects: may allocate from the grid's arena.
 */
void phys_spatial_grid_insert(phys_spatial_grid_t *grid, uint32_t body_index,
                              const phys_aabb_t *aabb);

/**
 * @brief Query the grid for all bodies overlapping the given AABB.
 *
 * Collects unique body indices from all cells that the query AABB touches.
 *
 * @param grid        Grid (if NULL, returns 0).
 * @param aabb        Query AABB (if NULL, returns 0).
 * @param out_indices Output array for body indices (if NULL, returns 0).
 * @param max_results Maximum number of indices to write.
 * @return Number of unique body indices written to @p out_indices.
 *
 * Side effects: none (read-only query).
 */
uint32_t phys_spatial_grid_query(const phys_spatial_grid_t *grid,
                                 const phys_aabb_t *aabb,
                                 uint32_t *out_indices, uint32_t max_results);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_SPATIAL_GRID_H */
