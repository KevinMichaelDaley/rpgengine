#ifndef FERRUM_PHYSICS_TIER_LIST_H
#define FERRUM_PHYSICS_TIER_LIST_H

/** @file
 * @brief Tier list structures for T0-T5 simulation tiers.
 *
 * Tier lists are packed index arrays pointing into the shared body pool.
 * They are arena-allocated each frame — no malloc during tick.
 * T0-T4 are "active" tiers; T5 is sleeping/dormant.
 */

#include <stdbool.h>
#include <stdint.h>

struct phys_frame_arena; /* forward declaration */

#ifdef __cplusplus
extern "C" {
#endif

/** Number of simulation tiers (ANIM + T0-T5). */
#define PHYS_TIER_COUNT 7

/**
 * @brief Simulation tier identifiers.
 *
 * Each tier represents a different level of physics fidelity.
 * ANIM is the lowest numeric value so it wins min-tier island
 * promotion, pulling the whole island to XPBD.
 */
typedef enum phys_tier {
    PHYS_TIER_ANIM = 0,        /**< Animated / ragdoll bodies (XPBD). */
    PHYS_TIER_0_DIRECT,        /**< Direct manipulation. */
    PHYS_TIER_1_NEAR,          /**< Near interactive. */
    PHYS_TIER_2_VISIBLE,       /**< Visible / hazardous. */
    PHYS_TIER_3_WORLD,         /**< World-shaping. */
    PHYS_TIER_4_BACKGROUND,    /**< Background dynamic. */
    PHYS_TIER_5_SLEEPING,      /**< Sleeping / dormant. */
} phys_tier_t;

/* ── Tier list ──────────────────────────────────────────────────── */

/**
 * @brief A single tier's body index list (arena-allocated).
 *
 * Ownership: the indices array is owned by the frame arena that
 * allocated it.  The list itself is a value type (no heap ownership).
 *
 * Nullability: indices may be NULL if arena allocation failed.
 */
typedef struct phys_tier_list {
    uint32_t *indices;  /**< Arena-allocated array of body indices. */
    uint32_t count;     /**< Number of indices currently stored. */
    uint32_t capacity;  /**< Maximum number of indices. */
} phys_tier_list_t;

/**
 * @brief All tier lists for a single frame.
 *
 * Ownership: value type wrapping PHYS_TIER_COUNT tier lists.
 * The underlying index arrays are arena-owned.
 */
typedef struct phys_tier_lists {
    phys_tier_list_t tiers[PHYS_TIER_COUNT]; /**< Per-tier body lists. */
} phys_tier_lists_t;

/* ── API ────────────────────────────────────────────────────────── */

/**
 * @brief Initialize all tier lists, allocating index arrays from the arena.
 *
 * Each tier receives an indices array with capacity = max_bodies.
 * If arena allocation fails for a tier, that tier's capacity is set to 0
 * and its indices pointer is NULL.
 *
 * @param lists      Tier lists to initialize (NULL-safe, no-op if NULL).
 * @param arena      Frame arena to allocate from (NULL-safe, no-op if NULL).
 * @param max_bodies Maximum number of body indices per tier.
 *
 * Ownership: index arrays are owned by the arena.
 * Side effects: allocates from the arena's bump pointer.
 */
void phys_tier_lists_init(phys_tier_lists_t *lists,
                          struct phys_frame_arena *arena,
                          uint32_t max_bodies);

/**
 * @brief Add a body index to a tier list.
 *
 * If the list is at capacity, the add is silently ignored.
 *
 * @param list       Tier list to add to (NULL-safe, no-op if NULL).
 * @param body_index Body pool index to append.
 *
 * Side effects: increments list->count.
 */
void phys_tier_list_add(phys_tier_list_t *list, uint32_t body_index);

/**
 * @brief Clear a single tier list (O(1) — resets count to zero).
 *
 * @param list  Tier list to clear (NULL-safe, no-op if NULL).
 */
void phys_tier_list_clear(phys_tier_list_t *list);

/**
 * @brief Clear all tier lists (O(PHYS_TIER_COUNT)).
 *
 * @param lists  Tier lists to clear (NULL-safe, no-op if NULL).
 */
void phys_tier_lists_clear_all(phys_tier_lists_t *lists);

/**
 * @brief Count the total number of active (non-sleeping) bodies.
 *
 * Sums counts for tiers T0 through T4.  T5 (sleeping) is excluded.
 *
 * @param lists  Tier lists to query (NULL returns 0).
 * @return Total body count across active tiers.
 */
uint32_t phys_tier_lists_total_active(const phys_tier_lists_t *lists);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_TIER_LIST_H */
