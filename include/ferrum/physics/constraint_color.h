/**
 * @file constraint_color.h
 * @brief Greedy graph coloring for constraint batching.
 *
 * Given a set of constraints (each linking two bodies), assigns a
 * color to each constraint such that no two constraints sharing a
 * body receive the same color.  Constraints of the same color can
 * be solved in parallel without write contention on body velocities.
 *
 * Algorithm: greedy lowest-degree-first ordering.  Each constraint
 * node's degree is the number of other constraints that share at
 * least one body.  Constraints are colored in ascending degree order,
 * each receiving the smallest color not used by its already-colored
 * neighbors.
 *
 * All workspace is arena-allocated (no malloc on the hot path).
 */

#ifndef FERRUM_PHYSICS_CONSTRAINT_COLOR_H
#define FERRUM_PHYSICS_CONSTRAINT_COLOR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations. */
struct phys_constraint;
struct phys_frame_arena;

/**
 * @brief Result of a constraint coloring pass.
 *
 * Ownership: all arrays are arena-allocated; lifetime is tied to the
 * arena passed to phys_constraint_color().
 */
typedef struct phys_color_result {
    uint32_t *colors;       /**< Per-constraint color assignment (0-based). */
    uint32_t  num_colors;   /**< Total number of distinct colors used. */
    uint32_t  count;        /**< Number of constraints colored. */
} phys_color_result_t;

/**
 * @brief Assign colors to constraints via greedy lowest-degree-first.
 *
 * Two constraints are adjacent (conflicting) if they share at least
 * one body index.  The algorithm:
 *   1. Compute per-constraint degree (neighbor count).
 *   2. Sort constraints by ascending degree.
 *   3. Greedily assign the smallest available color.
 *
 * @param constraints       Array of constraints to color.
 * @param constraint_count  Number of constraints.
 * @param body_count        Total body count (for indexing workspace).
 * @param arena             Frame arena for all temporary allocations.
 * @param result_out        Output result (filled on success).
 * @return 0 on success, -1 on invalid args or arena exhaustion.
 *
 * @note Side effects: allocates from arena.
 * @note No per-frame malloc/free.
 */
int phys_constraint_color(const struct phys_constraint *constraints,
                          uint32_t constraint_count,
                          uint32_t body_count,
                          struct phys_frame_arena *arena,
                          phys_color_result_t *result_out);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_CONSTRAINT_COLOR_H */
