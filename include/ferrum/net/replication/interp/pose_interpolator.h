#ifndef FERRUM_NET_REPLICATION_INTERP_POSE_INTERPOLATOR_H
#define FERRUM_NET_REPLICATION_INTERP_POSE_INTERPOLATOR_H

#include <stdbool.h>

#include "ferrum/math/quat.h"
#include "ferrum/math/vec3.h"

/** @file
 * @brief Time-based pose interpolation for replicated entities.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Pose interpolator tracking the last two received snapshots.
 *  Stores implied linear and angular velocity for extrapolation. */
typedef struct fr_pose_interpolator {
    bool has_prev;
    bool has_curr;
    double prev_time_s;
    double curr_time_s;
    vec3_t prev_pos;
    vec3_t curr_pos;
    quat_t prev_rot;
    quat_t curr_rot;
    vec3_t implied_vel;     /**< Linear velocity implied by last two snapshots. */
    vec3_t implied_ang_vel; /**< Angular velocity (axis * angle/dt) implied by last two snapshots. */
    vec3_t server_vel;      /**< Server-authoritative linear velocity (m/s). */
    vec3_t server_ang_vel;  /**< Server-authoritative angular velocity (rad/s). */
} fr_pose_interpolator_t;

/**
 * @brief Reset the interpolator to an empty state.
 * @param interp Interpolator to reset (non-NULL).
 */
void fr_pose_interpolator_reset(fr_pose_interpolator_t *interp);

/**
 * @brief Push a received snapshot into the interpolator.
 *
 * Ownership: no ownership transfer.
 * Nullability: interp must be non-NULL.
 * Error semantics: returns false on invalid input.
 * Side effects: updates the tracked snapshots.
 *
 * @param interp Interpolator (non-NULL).
 * @param recv_time_s Monotonic receive timestamp in seconds.
 * @param pos Position.
 * @param rot Rotation quaternion.
 * @param vel Server-authoritative linear velocity (m/s).
 * @param ang_vel Server-authoritative angular velocity (rad/s).
 * @return true on success.
 */
bool fr_pose_interpolator_push(fr_pose_interpolator_t *interp, double recv_time_s,
                               vec3_t pos, quat_t rot, vec3_t vel, vec3_t ang_vel);

/**
 * @brief Sample interpolated pose at a given time.
 *
 * If only one snapshot is available, returns that snapshot.
 * If two snapshots are available, interpolates by time and clamps outside
 * the [prev,curr] window.
 *
 * @param interp Interpolator (non-NULL).
 * @param now_time_s Monotonic sample time in seconds.
 * @param quat_epsilon Epsilon guard passed to quaternion normalization.
 * @param out_pos Output position (non-NULL).
 * @param out_rot Output rotation (non-NULL).
 * @return true if a pose was produced, false if no snapshots exist or args invalid.
 */
bool fr_pose_interpolator_sample(const fr_pose_interpolator_t *interp,
                                double now_time_s,
                                float quat_epsilon,
                                vec3_t *out_pos,
                                quat_t *out_rot);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_NET_REPLICATION_INTERP_POSE_INTERPOLATOR_H */
