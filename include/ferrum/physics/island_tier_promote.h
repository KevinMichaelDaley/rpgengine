/**
 * @file island_tier_promote.h
 * @brief Stage 10b: Island Tier Promotion.
 *
 * After islands are built (Stage 10), this pass promotes all bodies
 * in each island to the highest-fidelity (lowest-numbered) tier
 * found in that island.  This guarantees per-island solver-mode
 * uniformity: every constraint in an island targets the same solver.
 *
 * Constraints' solver_mode fields are updated to match the promoted
 * tier so the TGS/XPBD dispatch can operate per-island.
 */

#ifndef FERRUM_PHYSICS_ISLAND_TIER_PROMOTE_H
#define FERRUM_PHYSICS_ISLAND_TIER_PROMOTE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations. */
struct phys_island_list;
struct phys_body;
struct phys_constraint;

/**
 * @brief Arguments for the island tier promotion pass.
 *
 * Ownership: caller owns all pointed-to data.
 * Nullability: all pointers must be non-NULL for the stage to execute.
 */
typedef struct phys_island_tier_promote_args {
    struct phys_island_list *islands;     /**< Island list (read-only topology). */
    struct phys_body *bodies;             /**< Body array — tier field is updated. */
    uint32_t body_count;                  /**< Total number of bodies. */
    struct phys_constraint *constraints;  /**< Constraint array — solver_mode updated. */
    uint32_t constraint_count;            /**< Number of constraints. */
} phys_island_tier_promote_args_t;

/**
 * @brief Execute Stage 10b: Island Tier Promotion.
 *
 * For each non-sleeping island, finds the minimum tier among all
 * bodies and sets every body's tier to that minimum.  Then updates
 * every constraint's solver_mode to match the promoted tier.
 *
 * Static bodies (inv_mass == 0) are excluded from min-tier
 * computation but their tier is NOT modified.
 *
 * @param args  Stage arguments.  NULL-safe (no-op).
 *
 * @note Side effects: modifies body tier fields and constraint solver_mode.
 * @note No allocations performed.
 */
void phys_stage_island_tier_promote(const phys_island_tier_promote_args_t *args);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_ISLAND_TIER_PROMOTE_H */
