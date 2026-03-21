/**
 * @file scene_gizmo_bone.h
 * @brief Per-bone gizmo mode: independent gizmos for selected skeleton bones.
 *
 * When in per-object gizmo mode with bone selection active, each
 * selected bone gets its own transform gizmo positioned at the bone's
 * world-space head position.
 *
 * Ownership: per_bone_gizmo_t array is caller-allocated.
 * Nullability: all functions are NULL-safe (return 0 / no-op).
 * Error semantics: build returns 0 on empty selection or NULL args.
 * Side effects: apply_drag/apply_rotate modify skeleton_def in place.
 *
 * Public types: per_bone_gizmo_t (1 / 2-type rule).
 */
#ifndef FERRUM_EDITOR_SCENE_GIZMO_BONE_H
#define FERRUM_EDITOR_SCENE_GIZMO_BONE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "ferrum/editor/viewport/viewport_gizmo.h"
#include "ferrum/math/vec3.h"
#include "ferrum/math/quat.h"

/* Forward declarations. */
struct skeleton_def;
struct edit_bone_selection;
struct mat4;
struct viewport_render_state;

/** @brief Convenience alias matching test expectations. */
#define GIZMO_TRANSLATE GIZMO_MODE_TRANSLATE

/**
 * @brief Per-bone gizmo entry: bone index + its own gizmo state.
 */
typedef struct per_bone_gizmo {
    uint32_t      bone_index; /**< Bone index in the skeleton. */
    gizmo_state_t gizmo;     /**< Independent gizmo state. */
} per_bone_gizmo_t;

/* ---- Build (scene_gizmo_bone.c) ---- */

/**
 * @brief Build per-bone gizmo states for all selected bones.
 *
 * Creates one gizmo per selected bone, positioned at the bone's
 * world-space head position (rest_world translation * entity model).
 *
 * @param skel          Skeleton definition (NULL-safe: returns 0).
 * @param bone_sel      Bone selection set (NULL-safe: returns 0).
 * @param entity_model  Entity model matrix for world transform (NULL-safe: returns 0).
 * @param mode          Gizmo mode (translate/rotate/scale).
 * @param out           Output array (caller-allocated).
 * @param capacity      Maximum entries in out array.
 * @return Number of gizmos written.
 */
uint32_t per_bone_gizmo_build(
    const struct skeleton_def *skel,
    const struct edit_bone_selection *bone_sel,
    const struct mat4 *entity_model,
    gizmo_mode_t mode,
    per_bone_gizmo_t *out,
    uint32_t capacity);

/**
 * @brief Hit-test all per-bone gizmos and return the index of the hit.
 *
 * @param gizmos       Per-bone gizmo array.
 * @param count        Number of entries.
 * @param ray          World-space ray.
 * @param gizmo_scale  Visual scale for hit testing.
 * @return Index into gizmos array of the hit, or -1 on miss.
 */
int32_t per_bone_gizmo_pick(
    const per_bone_gizmo_t *gizmos,
    uint32_t count,
    const struct editor_ray *ray,
    float gizmo_scale);

/* ---- Draw (scene_gizmo_bone_draw.c) ---- */

/**
 * @brief Draw per-bone gizmos for all selected bones.
 *
 * Renders an independent transform gizmo at each bone's world position
 * using the existing viewport_render_draw_gizmo() per bone gizmo.
 *
 * @param vp       Viewport render state (NULL-safe: no-op).
 * @param gizmos   Per-bone gizmo array (NULL-safe: no-op).
 * @param count    Number of entries in gizmos array.
 * @param view     View matrix (NULL-safe: no-op).
 * @param proj     Projection matrix (NULL-safe: no-op).
 * @param eye_pos  Camera eye position (may be NULL, reserved for future use).
 */
struct edit_selection;

void scene_gizmo_bone_draw(
    struct viewport_render_state *vp,
    const per_bone_gizmo_t *gizmos,
    uint32_t count,
    const struct mat4 *view,
    const struct mat4 *proj,
    const vec3_t *eye_pos,
    const struct edit_selection *selection);

/* ---- Apply (scene_gizmo_bone_apply.c) ---- */

/**
 * @brief Apply translate drag delta to a bone's rest pose.
 *
 * Translates the bone's rest_world matrix and tail_positions by delta.
 * Out-of-range bone_index or NULL skel is a no-op.
 *
 * @param skel        Skeleton definition (NULL-safe: no-op).
 * @param bone_index  Target bone index.
 * @param delta       World-space translation delta.
 */
void per_bone_gizmo_apply_drag(
    struct skeleton_def *skel,
    uint32_t bone_index,
    vec3_t delta);

/**
 * @brief Apply rotation to a bone's rest pose.
 *
 * Composes the incremental rotation into the bone's rest_world
 * orientation columns. Out-of-range bone_index or NULL skel is a no-op.
 *
 * @param skel        Skeleton definition (NULL-safe: no-op).
 * @param bone_index  Target bone index.
 * @param dq          Incremental rotation quaternion.
 */
void per_bone_gizmo_apply_rotate(
    struct skeleton_def *skel,
    uint32_t bone_index,
    quat_t dq);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_SCENE_GIZMO_BONE_H */
