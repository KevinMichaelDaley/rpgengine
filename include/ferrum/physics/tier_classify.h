/**
 * @file tier_classify.h
 * @brief Stage 1 — Base tier classification for physics bodies.
 *
 * Assigns each active body to a simulation tier (T0–T5) based on
 * body flags and game state.  Phase 1 only distinguishes static
 * (excluded), sleeping (T5), and dynamic (T0).
 */
#ifndef FERRUM_PHYSICS_TIER_CLASSIFY_H
#define FERRUM_PHYSICS_TIER_CLASSIFY_H

#include <stdint.h>

struct phys_body;
struct phys_game_state;
struct phys_tier_lists;
struct phys_frame_arena;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Arguments for the tier classification stage.
 *
 * Ownership: borrows all pointers; does not free anything.
 * Nullability: bodies and active may be NULL when body_count is 0.
 *              tier_lists_out and arena must be non-NULL for useful work.
 */
typedef struct phys_tier_classify_args {
    const struct phys_body *bodies;     /**< Body array (pool read buffer). */
    const uint8_t *active;             /**< Per-slot activity flags (1 = in use). */
    uint32_t body_count;               /**< Capacity of the body pool. */
    const struct phys_game_state *game; /**< Game state (unused in Phase 1). */
    struct phys_tier_lists *tier_lists_out; /**< Output tier lists to populate. */
    struct phys_frame_arena *arena;    /**< Frame arena for tier list allocation. */
} phys_tier_classify_args_t;

/**
 * @brief Run the tier classification stage.
 *
 * Initializes tier lists from the arena, then iterates all body slots.
 * Inactive slots are skipped.  Static bodies are excluded from all
 * tier lists.  Sleeping bodies go to T5; all other dynamic bodies
 * go to T0.
 *
 * @param args  Classification arguments (NULL-safe, no-op if NULL).
 *
 * Side effects: allocates from args->arena; populates args->tier_lists_out.
 */
void phys_stage_tier_classify(const phys_tier_classify_args_t *args);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_TIER_CLASSIFY_H */
