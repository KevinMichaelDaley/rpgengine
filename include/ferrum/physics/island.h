#ifndef FERRUM_PHYSICS_ISLAND_H
#define FERRUM_PHYSICS_ISLAND_H

/** @file
 * @brief Island structures and union-find for connected component discovery.
 *
 * Islands group bodies and constraints into independent connected components,
 * enabling parallel constraint solving with zero write contention.
 */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations. */
struct phys_frame_arena;
struct phys_constraint;

/**
 * @brief A connected component of bodies and constraints.
 *
 * Each island contains arrays of body and constraint indices that
 * reference into the global body pool and constraint arrays.
 *
 * Ownership: the index arrays are arena-allocated and owned by the
 * arena that was passed to phys_island_list_build().
 */
typedef struct phys_island {
    uint32_t *body_indices;       /**< Arena-allocated array of body indices. */
    uint32_t body_count;          /**< Number of bodies in this island. */
    uint32_t *constraint_indices; /**< Arena-allocated array of constraint indices. */
    uint32_t constraint_count;    /**< Number of constraints in this island. */
    bool sleeping;                /**< True if all bodies in the island are sleeping. */
} phys_island_t;

/**
 * @brief List of islands with union-find workspace.
 *
 * The union-find arrays (parent, rank) are arena-allocated and reused
 * across frames via clear + rebuild.
 *
 * Ownership: the list does not own any memory; all allocations come
 * from the arena passed at init time.
 */
typedef struct phys_island_list {
    phys_island_t *islands; /**< Arena-allocated array of islands. */
    uint32_t count;         /**< Number of active islands. */
    uint32_t capacity;      /**< Maximum number of islands. */

    /* Union-find workspace (arena-allocated). */
    uint32_t *parent;       /**< Parent array for union-find. */
    uint32_t *rank;         /**< Rank array for union-by-rank. */
    uint32_t uf_size;       /**< Size of the union-find arrays. */
} phys_island_list_t;

/**
 * @brief Initialize an island list with arena-allocated workspace.
 *
 * Allocates islands array, parent array, and rank array from the arena.
 * Initializes union-find so that each element is its own root.
 *
 * @param list        Island list to initialize. NULL-safe (no-op).
 * @param arena       Frame arena for allocations. NULL-safe (no-op).
 * @param max_bodies  Maximum number of bodies (union-find size).
 * @param max_islands Maximum number of islands to support.
 *
 * @note Ownership: all allocations come from the arena.
 * @note Side effects: allocates from arena.
 */
void phys_island_list_init(phys_island_list_t *list,
                           struct phys_frame_arena *arena,
                           uint32_t max_bodies,
                           uint32_t max_islands);

/**
 * @brief Clear the island list and reset union-find state.
 *
 * Resets count to 0 and re-initializes parent[i] = i, rank[i] = 0.
 *
 * @param list  Island list to clear. NULL-safe (no-op).
 *
 * @note Side effects: none beyond resetting list state.
 */
void phys_island_list_clear(phys_island_list_t *list);

/**
 * @brief Build islands from a constraint array using union-find.
 *
 * Groups bodies into connected components based on constraint pairs.
 * Only bodies that appear in at least one constraint are included.
 * Allocates per-island index arrays from the arena.
 *
 * @param list             Island list (must be initialized). NULL-safe (no-op).
 * @param constraints      Array of constraints. NULL with count=0 is valid.
 * @param constraint_count Number of constraints.
 * @param body_count       Total number of bodies in the simulation.
 * @param arena            Frame arena for per-island allocations. NULL-safe (no-op).
 *
 * @note Ownership: per-island arrays are arena-allocated.
 * @note Side effects: modifies list and allocates from arena.
 */
void phys_island_list_build(phys_island_list_t *list,
                            const struct phys_constraint *constraints,
                            uint32_t constraint_count,
                            uint32_t body_count,
                            struct phys_frame_arena *arena);

/**
 * @brief Find the root representative of element x with path compression.
 *
 * Uses path halving for amortized near-constant time.
 *
 * @param list  Island list containing union-find state. NULL returns x.
 * @param x     Element index. Out-of-bounds returns x.
 * @return Root representative of x.
 *
 * @note Side effects: modifies parent array (path compression).
 */
uint32_t phys_uf_find(phys_island_list_t *list, uint32_t x);

/**
 * @brief Union two elements by rank.
 *
 * Merges the sets containing x and y. No-op if they are already
 * in the same set.
 *
 * @param list  Island list containing union-find state. NULL-safe (no-op).
 * @param x     First element index.
 * @param y     Second element index.
 *
 * @note Side effects: modifies parent and rank arrays.
 */
void phys_uf_union(phys_island_list_t *list, uint32_t x, uint32_t y);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_ISLAND_H */
