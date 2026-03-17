/**
 * @file scene_gizmo_per_object.c
 * @brief Per-object gizmo build and hit-test.
 *
 * Non-static functions (2 / 4 limit):
 *   per_object_gizmo_build
 *   per_object_gizmo_pick
 */

#include "ferrum/editor/scene/scene_gizmo_per_object.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_selection.h"
#include "ferrum/editor/viewport/viewport_gizmo.h"
#include "ferrum/editor/viewport/transform_basis.h"
#include "ferrum/editor/viewport/viewport_camera.h"
#include "ferrum/math/mat4.h"
#include "ferrum/math/quat.h"
#include "ferrum/math/vec3.h"

#include <math.h>

uint32_t per_object_gizmo_build(
    const edit_entity_store_t *entities,
    const edit_selection_t *selection,
    gizmo_mode_t mode,
    transform_basis_t basis,
    const mat4_t *view,
    const vec3_t *eye_pos,
    per_object_gizmo_t *out,
    uint32_t capacity)
{
    if (!entities || !selection || !out || capacity == 0) return 0;

    uint32_t count = 0;
    uint32_t cap = entities->capacity;

    for (uint32_t i = 0; i < cap && count < capacity; ++i) {
        if (!edit_selection_contains(selection, i)) continue;
        const edit_entity_t *ent = edit_entity_store_get(entities, i);
        if (!ent || ent->hidden) continue;

        per_object_gizmo_t *pg = &out[count];
        pg->entity_id = i;

        gizmo_state_init(&pg->gizmo);
        pg->gizmo.mode = mode;
        pg->gizmo.position = (vec3_t){
            ent->pos[0], ent->pos[1], ent->pos[2]};

        /* Compute per-entity orientation.
         * In local basis, each entity uses its own orientation.
         * In global/cursor basis, all use identity.
         * Scale mode always forces local basis. */
        transform_basis_t effective = basis;
        if (mode == GIZMO_MODE_SCALE) {
            effective = TRANSFORM_BASIS_LOCAL;
        }

        const quat_t *orient = NULL;
        if (effective == TRANSFORM_BASIS_LOCAL) {
            orient = &ent->orientation;
        }
        pg->gizmo.orientation = transform_basis_orientation(
            effective, orient, view);

        /* Update arc quadrants if eye position is available. */
        if (eye_pos) {
            gizmo_update_arc_quadrants(&pg->gizmo, *eye_pos);
        }

        count++;
    }

    return count;
}

/**
 * @brief Pick rotation rings across all gizmos using two-stage logic.
 *
 * Stage 1: Check if the cursor is within RING_HIT_THRESHOLD of ANY
 *          arc on ANY gizmo. This determines whether the click is a
 *          gizmo interaction at all.
 * Stage 2: Among all arcs across all gizmos, pick the one with the
 *          smallest screen-space distance to the cursor.
 *
 * This ensures the globally closest ring wins, not just the closest
 * ring within whichever gizmo happened to be tested first.
 */
static uint32_t pick_rotation_rings_(
    const per_object_gizmo_t *gizmos,
    uint32_t count,
    float gizmo_scale,
    const mat4_t *vp,
    float screen_x,
    float screen_y,
    gizmo_axis_t *out_axis)
{
    static const gizmo_axis_t ring_axes[3] = {
        GIZMO_AXIS_X, GIZMO_AXIS_Y, GIZMO_AXIS_Z
    };

    /* Stage 1: find the global minimum screen distance across all
     * gizmos × 3 rings. */
    float global_best_dist = 1e30f;
    uint32_t best_gizmo_idx = UINT32_MAX;
    int best_ring = -1;

    for (uint32_t gi = 0; gi < count; gi++) {
        float dists[3];
        gizmo_ring_screen_distances(&gizmos[gi].gizmo, gizmo_scale,
                                      vp, screen_x, screen_y, dists);

        for (int ri = 0; ri < 3; ri++) {
            if (dists[ri] < global_best_dist) {
                global_best_dist = dists[ri];
                best_gizmo_idx = gi;
                best_ring = ri;
            }
        }
    }

    /* Stage 2: only accept if the closest ring is within threshold. */
    if (best_ring >= 0 && global_best_dist < GIZMO_RING_HIT_THRESHOLD) {
        *out_axis = ring_axes[best_ring];
        return gizmos[best_gizmo_idx].entity_id;
    }

    *out_axis = GIZMO_AXIS_NONE;
    return EDIT_ENTITY_INVALID_ID;
}

uint32_t per_object_gizmo_pick(
    const per_object_gizmo_t *gizmos,
    uint32_t count,
    const editor_ray_t *ray,
    float gizmo_scale,
    const mat4_t *vp,
    float screen_x,
    float screen_y,
    gizmo_axis_t *out_axis)
{
    if (!gizmos || !ray || !vp || !out_axis || count == 0) {
        if (out_axis) *out_axis = GIZMO_AXIS_NONE;
        return EDIT_ENTITY_INVALID_ID;
    }

    /* Rotation mode: use two-stage cross-gizmo ring selection. */
    if (count > 0 && gizmos[0].gizmo.mode == GIZMO_MODE_ROTATE) {
        return pick_rotation_rings_(gizmos, count, gizmo_scale,
                                      vp, screen_x, screen_y, out_axis);
    }

    /* Translate/Scale: per-gizmo hit test, pick closest by screen-space
     * distance (3D distance to camera as tiebreaker). */
    uint32_t best_entity = EDIT_ENTITY_INVALID_ID;
    gizmo_axis_t best_axis = GIZMO_AXIS_NONE;
    float best_dist_sq = 1e30f;

    for (uint32_t i = 0; i < count; i++) {
        gizmo_axis_t axis = gizmo_hit_test(
            &gizmos[i].gizmo, ray, gizmo_scale,
            vp, screen_x, screen_y);

        if (axis != GIZMO_AXIS_NONE) {
            /* Use squared distance from ray origin to gizmo center
             * to pick the closest gizmo on tie. */
            float dx = gizmos[i].gizmo.position.x - ray->origin.x;
            float dy = gizmos[i].gizmo.position.y - ray->origin.y;
            float dz = gizmos[i].gizmo.position.z - ray->origin.z;
            float dist_sq = dx * dx + dy * dy + dz * dz;

            if (dist_sq < best_dist_sq) {
                best_dist_sq = dist_sq;
                best_entity = gizmos[i].entity_id;
                best_axis = axis;
            }
        }
    }

    *out_axis = best_axis;
    return best_entity;
}
