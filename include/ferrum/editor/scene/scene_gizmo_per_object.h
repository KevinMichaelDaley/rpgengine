/**
 * @file scene_gizmo_per_object.h
 * @brief Per-object gizmo mode: independent gizmos for each selected entity.
 *
 * In per-object mode, each selected entity gets its own transform gizmo.
 * The user picks whichever gizmo they want and transforms only that entity.
 *
 * Ownership: per_object_gizmo_t array is caller-allocated (stack or heap).
 * Nullability: all pointer params must be non-NULL.
 * Error semantics: build returns 0 on empty selection, pick returns
 *                  EDIT_ENTITY_INVALID_ID on miss.
 * Side effects: apply_drag/apply_rotate modify entity store in place.
 *
 * Public types: per_object_gizmo_t (1 / 2-type rule).
 */
#ifndef FERRUM_EDITOR_SCENE_GIZMO_PER_OBJECT_H
#define FERRUM_EDITOR_SCENE_GIZMO_PER_OBJECT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "ferrum/editor/viewport/viewport_gizmo.h"
#include "ferrum/editor/viewport/transform_basis.h"
#include "ferrum/math/vec3.h"
#include "ferrum/math/quat.h"

/* Forward declarations. */
struct edit_entity_store;
struct edit_selection;
struct editor_ray;
struct mat4;

/**
 * @brief Per-object gizmo entry: entity ID + its own gizmo state.
 */
typedef struct per_object_gizmo {
    uint32_t      entity_id;  /**< Entity this gizmo belongs to. */
    gizmo_state_t gizmo;      /**< Independent gizmo state. */
} per_object_gizmo_t;

/* ---- Build (scene_gizmo_per_object.c) ---- */

/**
 * @brief Build per-object gizmo states for all selected entities.
 *
 * Populates the output array with one gizmo per selected entity,
 * positioned at each entity's pos with the appropriate orientation.
 *
 * @param entities  Entity store (non-NULL).
 * @param selection Selection set (non-NULL).
 * @param mode      Gizmo mode (translate/rotate/scale).
 * @param basis     Transform basis for orientation computation.
 * @param view      View matrix (needed for VIEW basis; may be NULL otherwise).
 * @param eye_pos   Camera eye position (for arc quadrant update; may be NULL).
 * @param out       Output array (non-NULL, caller-allocated).
 * @param capacity  Maximum entries in out array.
 * @return Number of gizmos written (0 if selection is empty).
 */
uint32_t per_object_gizmo_build(
    const struct edit_entity_store *entities,
    const struct edit_selection *selection,
    gizmo_mode_t mode,
    transform_basis_t basis,
    const struct mat4 *view,
    const vec3_t *eye_pos,
    per_object_gizmo_t *out,
    uint32_t capacity);

/**
 * @brief Hit-test all per-object gizmos and return the picked entity.
 *
 * Iterates all gizmos, tests each against the ray, returns the entity
 * ID of the closest hit.
 *
 * @param gizmos     Per-object gizmo array (non-NULL).
 * @param count      Number of entries in gizmos.
 * @param ray        World-space ray (non-NULL).
 * @param gizmo_scale Visual scale for hit testing.
 * @param vp         View-projection matrix (non-NULL).
 * @param screen_x   Cursor X in normalized viewport coords [0,1].
 * @param screen_y   Cursor Y in normalized viewport coords [0,1].
 * @param out_axis   Receives the hit axis (non-NULL).
 * @return Entity ID of the picked gizmo, or EDIT_ENTITY_INVALID_ID.
 */
uint32_t per_object_gizmo_pick(
    const per_object_gizmo_t *gizmos,
    uint32_t count,
    const struct editor_ray *ray,
    float gizmo_scale,
    const struct mat4 *vp,
    float screen_x,
    float screen_y,
    gizmo_axis_t *out_axis);

/* ---- Apply (scene_gizmo_per_object_apply.c) ---- */

/**
 * @brief Apply translate or scale drag delta to a single entity.
 *
 * For GIZMO_MODE_TRANSLATE: adds delta to entity pos.
 * For GIZMO_MODE_SCALE: multiplies scale by (1 + delta).
 *
 * @param entities  Entity store (non-NULL).
 * @param entity_id Target entity ID.
 * @param mode      Current gizmo mode.
 * @param delta     World-space drag delta.
 */
void per_object_gizmo_apply_drag(
    struct edit_entity_store *entities,
    uint32_t entity_id,
    gizmo_mode_t mode,
    vec3_t delta);

/**
 * @brief Apply rotation quaternion to a single entity.
 *
 * Composes the incremental rotation with the entity's orientation
 * and updates the euler cache. If pivot is non-NULL, also orbits
 * the entity's position around the pivot point (cursor-basis mode).
 *
 * @param entities  Entity store (non-NULL).
 * @param entity_id Target entity ID.
 * @param dq        Incremental rotation quaternion.
 * @param pivot     If non-NULL, orbit entity position around this point.
 */
void per_object_gizmo_apply_rotate(
    struct edit_entity_store *entities,
    uint32_t entity_id,
    quat_t dq,
    const vec3_t *pivot);

/* ---- Draw (scene_gizmo_per_object_draw.c) ---- */

struct viewport_render_state;

/**
 * @brief Draw per-object gizmos for all selected entities.
 *
 * Builds temporary gizmo states for each selected entity and
 * renders them using viewport_render_draw_gizmo().
 *
 * @param vp         Viewport render state (non-NULL).
 * @param entities   Entity store (non-NULL).
 * @param selection  Selection set (non-NULL).
 * @param mode       Gizmo mode.
 * @param basis      Transform basis.
 * @param view       View matrix (non-NULL).
 * @param proj       Projection matrix (non-NULL).
 * @param eye_pos    Camera eye position (non-NULL).
 */
void scene_gizmo_per_object_draw(
    struct viewport_render_state *vp,
    const struct edit_entity_store *entities,
    const struct edit_selection *selection,
    gizmo_mode_t mode,
    transform_basis_t basis,
    const struct mat4 *view,
    const struct mat4 *proj,
    const vec3_t *eye_pos);

/* ---- Input (scene_gizmo_per_object_input.c) ---- */

struct scene_editor;
struct editor_ray;

/**
 * @brief Hit-test per-object gizmos and start drag if hit.
 *
 * Builds temp gizmo states, picks the nearest hit, sets up drag
 * state in the focused viewport for the picked entity.
 *
 * @param ed        Scene editor (non-NULL).
 * @param ray       World-space ray (non-NULL).
 * @param gizmo_scale Visual scale for hit testing.
 * @param vp_matrix View-projection matrix (non-NULL).
 * @param screen_x  Cursor X in normalized viewport coords [0,1].
 * @param screen_y  Cursor Y in normalized viewport coords [0,1].
 * @return true if a gizmo was hit, false otherwise.
 */
bool scene_per_object_gizmo_hit_test(
    struct scene_editor *ed,
    const struct editor_ray *ray,
    float gizmo_scale,
    const struct mat4 *vp_matrix,
    float screen_x,
    float screen_y);

/**
 * @brief Send per-entity absolute command for the dragged entity.
 *
 * Sends move_id/rotate_id/scale_id with absolute values for the
 * entity that was dragged in per-object mode.
 *
 * @param ed          Scene editor (non-NULL).
 * @param total_delta Accumulated drag delta (unused for abs commands).
 */
void scene_per_object_send_commands(
    struct scene_editor *ed,
    vec3_t total_delta);

/**
 * @brief Send an absolute transform command for a specific entity.
 *
 * @param ed         Scene editor (non-NULL).
 * @param entity_id  Entity to send command for.
 * @param mode       Gizmo mode (translate/rotate/scale).
 */
void scene_per_object_send_abs_command(
    struct scene_editor *ed,
    uint32_t entity_id,
    gizmo_mode_t mode);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_SCENE_GIZMO_PER_OBJECT_H */
