#ifndef FERRUM_PHYSICS_ISLAND_BUILD_H
#define FERRUM_PHYSICS_ISLAND_BUILD_H

/** @file
 * @brief Stage 10: Island Build — groups connected bodies into islands
 *        using union-find for parallel constraint solving.
 *
 * Static bodies are excluded from island merging: constraints involving
 * a static body do not cause the dynamic partner to merge with other
 * bodies connected through that static body.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations. */
struct phys_constraint;
struct phys_island_list;
struct phys_frame_arena;
struct phys_body;

/**
 * @brief Arguments for the island build stage.
 *
 * Ownership: caller owns all pointed-to data.  The stage allocates
 * island internals from the provided arena.
 *
 * Nullability: constraints may be NULL when constraint_count is 0.
 * All other pointers must be non-NULL for the stage to execute.
 */
typedef struct phys_island_build_args {
    const struct phys_constraint *constraints; /**< Constraint array. */
    uint32_t constraint_count;                 /**< Number of constraints. */
    const struct phys_body *bodies;            /**< Body array for static checks. */
    uint32_t body_count;                       /**< Total number of bodies. */
    struct phys_island_list *islands_out;       /**< Island list to populate. */
    struct phys_frame_arena *arena;             /**< Frame arena for allocations. */
} phys_island_build_args_t;

/**
 * @brief Execute Stage 10: Island Build.
 *
 * Initializes the island list, filters out constraints involving static
 * bodies, and builds connected-component islands from the remaining
 * dynamic-only constraint pairs using union-find.
 *
 * @param args  Stage arguments. NULL-safe (no-op).
 *
 * @note Side effects: modifies *args->islands_out, allocates from arena.
 * @note Ownership: per-island arrays are arena-allocated.
 */
void phys_stage_island_build(const phys_island_build_args_t *args);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_ISLAND_BUILD_H */
