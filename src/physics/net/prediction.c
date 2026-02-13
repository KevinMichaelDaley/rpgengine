/**
 * @file prediction.c
 * @brief Client prediction reconciliation implementation.
 *
 * Compares local physics body state against authoritative server snapshots
 * and corrects discrepancies via snapping (large errors) or blending
 * (small errors using lerp/slerp).
 */

#include "ferrum/physics/prediction.h"
#include "ferrum/physics/world.h"
#include "ferrum/physics/snapshot.h"
#include "ferrum/math/vec3.h"
#include "ferrum/math/quat.h"

#include <math.h>
#include <string.h>

/** Small epsilon below which position error is considered negligible. */
#define PREDICTION_EPSILON 0.001f

/** Scale for dequantizing snapshot velocity (mm/s → m/s). */
#define VEL_INV_SCALE (1.0f / 1000.0f)

/** Velocity blend rate — higher than position to reduce jitter fast. */
#define VEL_BLEND_RATE 0.3f

/* ── Public API ─────────────────────────────────────────────────── */

phys_prediction_config_t phys_prediction_config_default(void)
{
    phys_prediction_config_t cfg;
    cfg.position_snap_threshold = 0.5f;
    cfg.position_blend_rate     = 0.2f;
    cfg.rotation_snap_threshold = 0.5f;
    cfg.rotation_blend_rate     = 0.2f;
    return cfg;
}

phys_prediction_result_t phys_prediction_reconcile(
    struct phys_world *local_world,
    const struct phys_snapshot *server_snapshot,
    const phys_prediction_config_t *config)
{
    phys_prediction_result_t result;
    memset(&result, 0, sizeof(result));

    /* NULL safety: return zeroed result on any NULL input. */
    if (!local_world || !server_snapshot || !config) {
        return result;
    }

    uint32_t world_count = phys_world_body_count(local_world);
    uint32_t snap_count  = server_snapshot->body_count;
    uint32_t count = (world_count < snap_count) ? world_count : snap_count;

    for (uint32_t i = 0; i < count; i++) {
        phys_body_t *body = phys_world_get_body(local_world, i);
        if (!body) {
            continue;
        }

        const phys_snapshot_body_t *snap_body = &server_snapshot->bodies[i];

        /* Dequantize server position and orientation. */
        phys_vec3_t server_pos = phys_dequantize_vec3(
            snap_body->position, 1.0f / 1000.0f);
        phys_quat_t server_ori = phys_dequantize_quat(snap_body->orientation);

        /* Compute position error (Euclidean distance). */
        vec3_t diff = vec3_sub(
            VEC3_FROM_PHYS_VEC3(body->position),
            VEC3_FROM_PHYS_VEC3(server_pos));
        float pos_error = vec3_magnitude(diff);

        /* Compute rotation error: angle = 2 * acos(|dot(q1, q2)|). */
        float dot = body->orientation.x * server_ori.x
                  + body->orientation.y * server_ori.y
                  + body->orientation.z * server_ori.z
                  + body->orientation.w * server_ori.w;
        float abs_dot = fabsf(dot);
        if (abs_dot > 1.0f) {
            abs_dot = 1.0f;
        }
        float rot_error = 2.0f * acosf(abs_dot);

        /* Track max errors. */
        if (pos_error > result.max_position_error) {
            result.max_position_error = pos_error;
        }
        if (rot_error > result.max_rotation_error) {
            result.max_rotation_error = rot_error;
        }

        /* Decide: snap, blend, or correct. */
        if (pos_error > config->position_snap_threshold ||
            rot_error > config->rotation_snap_threshold) {
            /* Snap: teleport to server state including velocities. */
            body->position    = server_pos;
            body->orientation = server_ori;
            body->linear_vel  = phys_dequantize_vec3(
                snap_body->linear_vel, VEL_INV_SCALE);
            body->angular_vel = phys_dequantize_vec3(
                snap_body->angular_vel, VEL_INV_SCALE);
            result.bodies_snapped++;
        } else if (pos_error > PREDICTION_EPSILON) {
            /* Blend: lerp position toward server, slerp orientation. */
            vec3_t blended_pos = vec3_lerp(
                VEC3_FROM_PHYS_VEC3(body->position),
                VEC3_FROM_PHYS_VEC3(server_pos),
                config->position_blend_rate);
            body->position = PHYS_VEC3_FROM_VEC3(blended_pos);

            quat_t blended_ori = quat_slerp(
                QUAT_FROM_PHYS_QUAT(body->orientation),
                QUAT_FROM_PHYS_QUAT(server_ori),
                config->rotation_blend_rate,
                1e-6f);
            body->orientation = PHYS_QUAT_FROM_QUAT(blended_ori);

            /* Blend velocities toward server to reduce drift. */
            phys_vec3_t srv_lv = phys_dequantize_vec3(
                snap_body->linear_vel, VEL_INV_SCALE);
            phys_vec3_t srv_av = phys_dequantize_vec3(
                snap_body->angular_vel, VEL_INV_SCALE);
            body->linear_vel.x += (srv_lv.x - body->linear_vel.x) * VEL_BLEND_RATE;
            body->linear_vel.y += (srv_lv.y - body->linear_vel.y) * VEL_BLEND_RATE;
            body->linear_vel.z += (srv_lv.z - body->linear_vel.z) * VEL_BLEND_RATE;
            body->angular_vel.x += (srv_av.x - body->angular_vel.x) * VEL_BLEND_RATE;
            body->angular_vel.y += (srv_av.y - body->angular_vel.y) * VEL_BLEND_RATE;
            body->angular_vel.z += (srv_av.z - body->angular_vel.z) * VEL_BLEND_RATE;

            result.bodies_blended++;
        } else {
            result.bodies_correct++;
        }
    }

    return result;
}

bool phys_prediction_body_diverged(
    const phys_vec3_t *local_pos,
    const phys_vec3_t *server_pos,
    float snap_threshold)
{
    if (!local_pos || !server_pos) {
        return false;
    }

    vec3_t diff = vec3_sub(
        VEC3_FROM_PHYS_VEC3(*local_pos),
        VEC3_FROM_PHYS_VEC3(*server_pos));
    float dist = vec3_magnitude(diff);
    return dist > snap_threshold;
}
