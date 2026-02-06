#ifndef FERRUM_PHYSICS_PREDICTION_H
#define FERRUM_PHYSICS_PREDICTION_H

/**
 * @file prediction.h
 * @brief Client-side prediction and server reconciliation for networked physics.
 *
 * Clients run local physics simulation, receive authoritative server snapshots,
 * and reconcile discrepancies by snapping (large errors) or blending (small
 * errors) local body state toward the server-authoritative state.
 */

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/physics/phys_types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct phys_world;
struct phys_snapshot;

/**
 * @brief Configuration for prediction reconciliation thresholds and rates.
 *
 * Controls when bodies are snapped vs. blended and how fast blending occurs.
 */
typedef struct phys_prediction_config {
    float position_snap_threshold;   /**< Snap if position error > this (meters). Default 0.5. */
    float position_blend_rate;       /**< Lerp rate for small position errors. Default 0.1. */
    float rotation_snap_threshold;   /**< Snap if rotation error > this (radians). Default 0.5. */
    float rotation_blend_rate;       /**< Slerp rate for small rotation errors. Default 0.1. */
} phys_prediction_config_t;

/**
 * @brief Result of a prediction reconciliation pass.
 *
 * Reports how many bodies were snapped, blended, or left unchanged,
 * plus the maximum observed errors for telemetry/debugging.
 */
typedef struct phys_prediction_result {
    uint32_t bodies_snapped;    /**< Bodies teleported to server state. */
    uint32_t bodies_blended;    /**< Bodies being smoothly corrected. */
    uint32_t bodies_correct;    /**< Bodies within tolerance. */
    float max_position_error;   /**< Largest position error seen (meters). */
    float max_rotation_error;   /**< Largest rotation error seen (radians). */
} phys_prediction_result_t;

/**
 * @brief Return a default prediction config with reasonable values.
 *
 * Defaults: position_snap_threshold=0.5, position_blend_rate=0.1,
 *           rotation_snap_threshold=0.5, rotation_blend_rate=0.1.
 *
 * @return Default configuration struct.
 */
phys_prediction_config_t phys_prediction_config_default(void);

/**
 * @brief Reconcile local world state against an authoritative server snapshot.
 *
 * For each body in the server snapshot (up to the minimum of
 * snapshot body_count and world body_count):
 * - Dequantizes server position/orientation.
 * - Computes position error (Euclidean distance) and rotation error
 *   (angle between quaternions via 2*acos(|dot(q1,q2)|)).
 * - Snaps if either error exceeds its snap threshold.
 * - Blends (lerp/slerp) if position error exceeds a small epsilon (0.001).
 * - Otherwise marks the body as correct.
 *
 * @param local_world     Local physics world to correct (NULL returns zeroed result).
 * @param server_snapshot Authoritative server snapshot (NULL returns zeroed result).
 * @param config          Reconciliation config (NULL returns zeroed result).
 * @return Reconciliation result with counts and max errors.
 *
 * Ownership: does not take ownership of any parameter.
 * Side effects: modifies body positions/orientations in local_world.
 */
phys_prediction_result_t phys_prediction_reconcile(
    struct phys_world *local_world,
    const struct phys_snapshot *server_snapshot,
    const phys_prediction_config_t *config);

/**
 * @brief Check whether a body has diverged beyond a snap threshold.
 *
 * Simple Euclidean distance check between two positions.
 *
 * @param local_pos      Local body position (NULL returns false).
 * @param server_pos     Server body position (NULL returns false).
 * @param snap_threshold Distance threshold for divergence.
 * @return true if distance > snap_threshold, false otherwise.
 *
 * Ownership: does not take ownership; read-only access.
 * Side effects: none.
 */
bool phys_prediction_body_diverged(
    const phys_vec3_t *local_pos,
    const phys_vec3_t *server_pos,
    float snap_threshold);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_PREDICTION_H */
