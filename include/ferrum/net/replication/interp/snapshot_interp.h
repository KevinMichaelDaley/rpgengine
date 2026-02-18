/**
 * @file snapshot_interp.h
 * @brief Whole-world snapshot interpolation for client rendering.
 *
 * Maintains an array of per-body pose interpolators fed from decoded
 * server snapshots.  Each render frame, the client samples interpolated
 * positions/orientations for smooth display between server ticks.
 *
 * Two body update strategies coexist:
 *  - **Interpolated**: constrained bodies (joints, chains) purely lerp/slerp
 *    between the two most recent server snapshots.
 *  - **Predicted**: free dynamic bodies run local physics (integration only)
 *    between snapshots, then get reconciled when the next snapshot arrives.
 *
 * Types: fr_snapshot_interp_t, fr_snapshot_interp_config_t (2 types).
 */
#ifndef FERRUM_NET_REPLICATION_INTERP_SNAPSHOT_INTERP_H
#define FERRUM_NET_REPLICATION_INTERP_SNAPSHOT_INTERP_H

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/math/vec3.h"
#include "ferrum/math/quat.h"
#include "ferrum/net/replication/interp/pose_interpolator.h"

#ifdef __cplusplus
extern "C" {
#endif

struct phys_snapshot;

/**
 * @brief Configuration for snapshot interpolation.
 */
typedef struct fr_snapshot_interp_config {
    uint32_t max_bodies;         /**< Maximum body slots (must match server). */
    float    quat_epsilon;       /**< Epsilon for quat normalization (default 1e-6). */
} fr_snapshot_interp_config_t;

/**
 * @brief World-level snapshot interpolator.
 *
 * Owns an array of per-body pose interpolators and tracks which
 * bodies have received at least one snapshot update.
 */
typedef struct fr_snapshot_interp {
    fr_pose_interpolator_t *interps;  /**< Array [max_bodies]. */
    uint32_t                max_bodies;
    float                   quat_epsilon;
    double                  last_recv_time;  /**< Recv time of most recent snapshot. */
    uint64_t                last_tick;        /**< Server tick of most recent snapshot. */
} fr_snapshot_interp_t;

/**
 * @brief Create a snapshot interpolator.
 *
 * Allocates internal arrays.  Caller must destroy with
 * fr_snapshot_interp_destroy().
 *
 * @param cfg  Configuration (non-NULL, max_bodies >= 1).
 * @return Allocated interpolator, or NULL on failure.
 *
 * Ownership: caller owns the returned pointer.
 */
fr_snapshot_interp_t *fr_snapshot_interp_create(
    const fr_snapshot_interp_config_t *cfg);

/**
 * @brief Destroy a snapshot interpolator and free resources.
 *
 * Safe to call with NULL.
 */
void fr_snapshot_interp_destroy(fr_snapshot_interp_t *si);

/**
 * @brief Feed a decoded server snapshot into the interpolator.
 *
 * Dequantizes each body from the snapshot and pushes pos/rot/vel
 * into the corresponding per-body pose interpolator.
 *
 * Stale snapshots (tick <= last_tick) are silently dropped.
 *
 * @param si            Interpolator (non-NULL).
 * @param snapshot      Decoded server snapshot (non-NULL).
 * @param recv_time_s   Monotonic receive time in seconds.
 * @return Number of bodies updated, or 0 on error/stale.
 *
 * Ownership: does not take ownership of snapshot.
 * Side effects: updates internal interpolator state.
 */
uint32_t fr_snapshot_interp_push(fr_snapshot_interp_t *si,
                                 const struct phys_snapshot *snapshot,
                                 double recv_time_s);

/**
 * @brief Sample interpolated pose for a single body.
 *
 * @param si        Interpolator (non-NULL).
 * @param body_idx  Body pool index.
 * @param now_s     Current monotonic time in seconds.
 * @param out_pos   Output interpolated position (non-NULL).
 * @param out_rot   Output interpolated orientation (non-NULL).
 * @return true if a pose was produced, false if body_idx is out of
 *         range or no snapshots have been received for that body.
 */
bool fr_snapshot_interp_sample(const fr_snapshot_interp_t *si,
                               uint32_t body_idx,
                               double now_s,
                               vec3_t *out_pos,
                               quat_t *out_rot);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_NET_REPLICATION_INTERP_SNAPSHOT_INTERP_H */
