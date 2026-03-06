/**
 * @file track_damped.c
 * @brief Damped Track constraint evaluator.
 *
 * Non-static functions: 1 (eval_damped_track)
 */

#include "ferrum/animation/constraint_solver.h"
#include "ferrum/animation/constraint_types.h"
#include "ferrum/animation/constraint_params.h"
#include "ferrum/math/mat4.h"
#include "ferrum/math/vec3.h"
#include "ferrum/math/quat.h"
#include <math.h>

/**
 * @brief Get the unit vector for a constraint_axis_t.
 */
static vec3_t axis_to_vec3_(constraint_axis_t axis) {
    switch (axis) {
        case CONSTRAINT_AXIS_X: return (vec3_t){  1, 0, 0 };
        case CONSTRAINT_AXIS_NEG_X: return (vec3_t){ -1, 0, 0 };
        case CONSTRAINT_AXIS_Y: return (vec3_t){  0, 1, 0 };
        case CONSTRAINT_AXIS_NEG_Y: return (vec3_t){  0,-1, 0 };
        case CONSTRAINT_AXIS_Z: return (vec3_t){  0, 0, 1 };
        case CONSTRAINT_AXIS_NEG_Z: return (vec3_t){  0, 0,-1 };
        default:                    return (vec3_t){  0, 1, 0 };
    }
}

/**
 * @brief Damped Track evaluator.
 *
 * Rotates the owner using the smallest rotation so that its track axis
 * points toward the target bone's position. Preserves owner position.
 */
void eval_damped_track(const constraint_def_t *def,
                       const constraint_eval_ctx_t *ctx,
                       mat4_t *inout) {
    if (!def || !ctx || !inout) return;
    if (def->target_bone_idx == UINT32_MAX) return;
    if (def->target_bone_idx >= ctx->bone_count) return;

    vec3_t owner_pos = { inout->m[12], inout->m[13], inout->m[14] };
    vec3_t target_pos = {
        ctx->pose[def->target_bone_idx].m[12],
        ctx->pose[def->target_bone_idx].m[13],
        ctx->pose[def->target_bone_idx].m[14]
    };

    vec3_t dir = vec3_sub(target_pos, owner_pos);
    float mag = vec3_magnitude(dir);
    if (mag < 1e-6f) return; /* degenerate — no rotation */

    dir = vec3_scale(dir, 1.0f / mag);

    /* The track axis in owner's local space. */
    vec3_t track = axis_to_vec3_(def->params.damped_track.track_axis);

    /* Build rotation from track axis to direction. */
    float dot = vec3_dot(track, dir);
    if (dot > 0.9999f) return; /* already aligned */

    quat_t rot;
    if (dot < -0.9999f) {
        /* 180° rotation around a perpendicular axis. */
        vec3_t perp = { 1.0f, 0.0f, 0.0f };
        if (fabsf(track.x) > 0.9f) {
            perp = (vec3_t){ 0.0f, 1.0f, 0.0f };
        }
        vec3_t axis = vec3_cross(track, perp);
        axis = vec3_normalize_safe(axis, 1e-7f);
        rot = quat_from_axis_angle(axis, 3.14159265f, 1e-7f);
    } else {
        vec3_t axis = vec3_cross(track, dir);
        float angle = acosf(dot);
        rot = quat_from_axis_angle(axis, angle, 1e-7f);
    }

    mat4_t rot_mat;
    quat_to_mat4(rot, &rot_mat);

    /* Apply rotation to owner's upper 3x3. */
    mat4_t result = mat4_mul(rot_mat, *inout);

    /* Preserve original translation. */
    result.m[12] = owner_pos.x;
    result.m[13] = owner_pos.y;
    result.m[14] = owner_pos.z;

    *inout = result;
}
