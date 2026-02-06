#ifndef FERRUM_PHYSICS_AMORTIZED_H
#define FERRUM_PHYSICS_AMORTIZED_H

/** @file
 * @brief Amortized ticking support for T4 (background) bodies.
 *
 * T4 bodies only receive a physics tick every 3rd frame.  Between
 * tick frames their visual pose is interpolated from the last
 * snapshot to reduce per-frame cost for distant props.
 */

#include "ferrum/physics/phys_types.h"
#include "ferrum/physics/body.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Number of frames between T4 physics ticks. */
#define PHYS_T4_TICK_INTERVAL 3

/**
 * @brief Per-body snapshot state for amortized T4 interpolation.
 *
 * Stores the pose of every body at the last T4 tick frame so that
 * non-tick frames can blend between the snapshot and the current
 * (predicted) pose.
 *
 * Ownership: caller owns the struct; init allocates internal arrays,
 * destroy frees them.
 *
 * Nullability: prev_positions and prev_orientations are NULL after
 * destroy or if init fails.
 */
typedef struct phys_amortized_state {
    uint32_t     body_capacity;      /**< Max bodies this state can track. */
    phys_vec3_t *prev_positions;     /**< Position at start of last T4 tick. */
    phys_quat_t *prev_orientations;  /**< Orientation at start of last T4 tick. */
    uint32_t     last_tick_frame;    /**< Frame counter of last T4 physics tick. */
} phys_amortized_state_t;

/**
 * @brief Initialize amortized state for a given body capacity.
 *
 * Allocates prev_positions and prev_orientations arrays on the heap.
 *
 * @param state         State to initialize (non-NULL).
 * @param body_capacity Maximum number of bodies to track.
 * @return true on success, false on allocation failure or NULL state.
 *
 * Ownership: caller must call phys_amortized_destroy() to free.
 * Side effects: heap allocation.
 */
bool phys_amortized_init(phys_amortized_state_t *state,
                         uint32_t body_capacity);

/**
 * @brief Destroy amortized state and free internal arrays.
 *
 * @param state  State to destroy (NULL-safe, no-op if NULL).
 *
 * Side effects: frees heap memory; zeroes the struct.
 */
void phys_amortized_destroy(phys_amortized_state_t *state);

/**
 * @brief Snapshot T4 body poses before a physics tick.
 *
 * Should be called before the physics tick on T4 tick frames
 * (when current_frame % PHYS_T4_TICK_INTERVAL == 0).  Only T4
 * bodies have their poses stored; other tiers are ignored.
 *
 * On non-tick frames this function is a no-op.
 *
 * @param state         Amortized state (NULL-safe, no-op).
 * @param bodies        Body array to snapshot from (NULL-safe, no-op).
 * @param body_count    Number of bodies in the array.
 * @param current_frame Monotonic frame counter.
 *
 * Side effects: writes prev_positions, prev_orientations,
 *               last_tick_frame.
 */
void phys_amortized_snapshot(phys_amortized_state_t *state,
                             const phys_body_t *bodies,
                             uint32_t body_count,
                             uint32_t current_frame);

/**
 * @brief Interpolate T4 visual poses on non-tick frames.
 *
 * Computes alpha = (current_frame - last_tick_frame) / T4_TICK_INTERVAL
 * and blends between the snapshot pose and the current body pose.
 * Non-T4 bodies are copied verbatim to the output arrays.
 *
 * @param state         Amortized state (NULL-safe, no-op).
 * @param bodies        Current body array (NULL-safe, no-op).
 * @param body_count    Number of bodies.
 * @param current_frame Monotonic frame counter.
 * @param visual_pos    Output: interpolated positions (non-NULL, body_count).
 * @param visual_rot    Output: interpolated orientations (non-NULL, body_count).
 *
 * Side effects: writes visual_pos and visual_rot arrays.
 */
void phys_amortized_interpolate(const phys_amortized_state_t *state,
                                const phys_body_t *bodies,
                                uint32_t body_count,
                                uint32_t current_frame,
                                phys_vec3_t *visual_pos,
                                phys_quat_t *visual_rot);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_AMORTIZED_H */
