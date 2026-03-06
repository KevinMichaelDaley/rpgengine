/**
 * @file track_to.c
 * @brief Track To and Locked Track constraint evaluators.
 *
 * Non-static functions: 2 (eval_track_to, eval_locked_track)
 */

#include "ferrum/animation/constraint_solver.h"
#include "ferrum/animation/constraint_types.h"
#include "ferrum/animation/constraint_params.h"
#include "ferrum/math/mat4.h"
#include "ferrum/math/vec3.h"
#include <math.h>

/**
 * @brief Map constraint_axis_t to a column index (0=X, 1=Y, 2=Z).
 */
static int axis_to_column_(constraint_axis_t axis) {
    switch (axis) {
        case CONSTRAINT_AXIS_X: case CONSTRAINT_AXIS_NEG_X: return 0;
        case CONSTRAINT_AXIS_Y: case CONSTRAINT_AXIS_NEG_Y: return 1;
        case CONSTRAINT_AXIS_Z: case CONSTRAINT_AXIS_NEG_Z: return 2;
        default: return 1;
    }
}

/**
 * @brief Return -1.0 if the axis is negative, else 1.0.
 */
static float axis_sign_(constraint_axis_t axis) {
    switch (axis) {
        case CONSTRAINT_AXIS_NEG_X:
        case CONSTRAINT_AXIS_NEG_Y:
        case CONSTRAINT_AXIS_NEG_Z: return -1.0f;
        default: return 1.0f;
    }
}

/**
 * @brief Get a world-space unit vector for a constraint_axis_t.
 */
static vec3_t axis_unit_vec_(constraint_axis_t axis) {
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
 * @brief Track To evaluator.
 *
 * Points the owner's track axis at the target while keeping the up
 * axis aligned. Constructs an orthonormal basis using Gram-Schmidt.
 */
void eval_track_to(const constraint_def_t *def,
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
    if (mag < 1e-6f) return;
    dir = vec3_scale(dir, 1.0f / mag);

    /* Apply track axis sign (negative means invert direction). */
    float track_sign = axis_sign_(def->params.track_to.track_axis);
    dir = vec3_scale(dir, track_sign);

    /* Up axis reference vector. */
    vec3_t up = axis_unit_vec_(def->params.track_to.up_axis);

    /* Build orthonormal basis: track = dir, third = cross(track, up), up = cross(third, track). */
    vec3_t third = vec3_cross(dir, up);
    float third_mag = vec3_magnitude(third);
    if (third_mag < 1e-6f) {
        /* Degenerate: up axis is parallel to track. Pick an arbitrary perpendicular. */
        vec3_t perp = { 1.0f, 0.0f, 0.0f };
        if (fabsf(dir.x) > 0.9f) perp = (vec3_t){ 0.0f, 1.0f, 0.0f };
        third = vec3_cross(dir, perp);
        third_mag = vec3_magnitude(third);
        if (third_mag < 1e-6f) return;
    }
    third = vec3_scale(third, 1.0f / third_mag);
    up = vec3_cross(third, dir);
    up = vec3_normalize_safe(up, 1e-7f);

    /* Assign columns based on track and up axis indices. */
    int track_col = axis_to_column_(def->params.track_to.track_axis);
    int up_col = axis_to_column_(def->params.track_to.up_axis);

    /* The third column is the remaining one. */
    int third_col = 3 - track_col - up_col;
    if (third_col < 0 || third_col > 2 || third_col == track_col || third_col == up_col) {
        third_col = 0;
        if (third_col == track_col || third_col == up_col) third_col = 1;
        if (third_col == track_col || third_col == up_col) third_col = 2;
    }

    /* Extract owner scale. */
    float scale[3];
    for (int col = 0; col < 3; col++) {
        float x = inout->m[col * 4 + 0];
        float y = inout->m[col * 4 + 1];
        float z = inout->m[col * 4 + 2];
        scale[col] = sqrtf(x * x + y * y + z * z);
        if (scale[col] < 1e-7f) scale[col] = 1.0f;
    }

    /* Determine correct handedness: we want det > 0. */
    vec3_t check = vec3_cross(dir, up);
    float det_sign = vec3_dot(check, third);
    if (det_sign < 0.0f) {
        third = vec3_scale(third, -1.0f);
    }

    /* Assign vectors to matrix columns. */
    vec3_t axes[3];
    axes[track_col] = dir;
    axes[up_col] = up;
    axes[third_col] = third;

    for (int col = 0; col < 3; col++) {
        inout->m[col * 4 + 0] = axes[col].x * scale[col];
        inout->m[col * 4 + 1] = axes[col].y * scale[col];
        inout->m[col * 4 + 2] = axes[col].z * scale[col];
    }

    /* Preserve translation. */
    inout->m[12] = owner_pos.x;
    inout->m[13] = owner_pos.y;
    inout->m[14] = owner_pos.z;
}

/**
 * @brief Locked Track evaluator.
 *
 * Rotates around the lock axis so that the track axis points as
 * close to the target as possible. The lock axis remains unchanged.
 */
void eval_locked_track(const constraint_def_t *def,
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

    vec3_t to_target = vec3_sub(target_pos, owner_pos);
    float mag = vec3_magnitude(to_target);
    if (mag < 1e-6f) return;
    to_target = vec3_scale(to_target, 1.0f / mag);

    int lock_col = axis_to_column_(def->params.locked_track.lock_axis);
    int track_col = axis_to_column_(def->params.locked_track.track_axis);

    /* Lock axis stays fixed. */
    vec3_t lock_vec = {
        inout->m[lock_col * 4 + 0],
        inout->m[lock_col * 4 + 1],
        inout->m[lock_col * 4 + 2]
    };
    float lock_len = vec3_magnitude(lock_vec);
    if (lock_len < 1e-7f) return;
    vec3_t lock_dir = vec3_scale(lock_vec, 1.0f / lock_len);

    /* Project to_target onto the plane perpendicular to lock axis. */
    float dot = vec3_dot(to_target, lock_dir);
    vec3_t projected = vec3_sub(to_target, vec3_scale(lock_dir, dot));
    float proj_mag = vec3_magnitude(projected);
    if (proj_mag < 1e-6f) return;
    projected = vec3_scale(projected, 1.0f / proj_mag);

    /* Track direction. */
    float track_sign = axis_sign_(def->params.locked_track.track_axis);
    vec3_t track_dir = vec3_scale(projected, track_sign);

    /* Third axis = cross(lock, track) or cross(track, lock) for handedness. */
    int third_col = 3 - lock_col - track_col;
    if (third_col < 0 || third_col > 2 || third_col == lock_col || third_col == track_col) {
        third_col = 0;
        if (third_col == lock_col || third_col == track_col) third_col = 1;
        if (third_col == lock_col || third_col == track_col) third_col = 2;
    }

    vec3_t third_dir = vec3_cross(lock_dir, track_dir);
    float third_mag = vec3_magnitude(third_dir);
    if (third_mag < 1e-6f) return;
    third_dir = vec3_scale(third_dir, 1.0f / third_mag);

    /* Ensure right-handed coordinate system. */
    vec3_t check = vec3_cross(lock_dir, track_dir);
    if (vec3_dot(check, third_dir) < 0.0f) {
        third_dir = vec3_scale(third_dir, -1.0f);
    }

    /* Extract scales. */
    float scale[3];
    for (int col = 0; col < 3; col++) {
        float x = inout->m[col * 4 + 0];
        float y = inout->m[col * 4 + 1];
        float z = inout->m[col * 4 + 2];
        scale[col] = sqrtf(x * x + y * y + z * z);
        if (scale[col] < 1e-7f) scale[col] = 1.0f;
    }

    /* Assign axes to columns. */
    vec3_t axes[3];
    axes[lock_col] = lock_dir;
    axes[track_col] = track_dir;
    axes[third_col] = third_dir;

    for (int col = 0; col < 3; col++) {
        inout->m[col * 4 + 0] = axes[col].x * scale[col];
        inout->m[col * 4 + 1] = axes[col].y * scale[col];
        inout->m[col * 4 + 2] = axes[col].z * scale[col];
    }

    inout->m[12] = owner_pos.x;
    inout->m[13] = owner_pos.y;
    inout->m[14] = owner_pos.z;
}
