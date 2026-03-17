/**
 * @file scene_gizmo_per_object_draw.c
 * @brief Per-object gizmo rendering: draws independent gizmos for each
 *        selected entity.
 *
 * Non-static functions (1 / 4 limit):
 *   scene_gizmo_per_object_draw
 */

#include "ferrum/editor/scene/scene_gizmo_per_object.h"
#include "ferrum/editor/scene/scene_viewport_render.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_selection.h"
#include "ferrum/editor/viewport/viewport_gizmo.h"
#include "ferrum/editor/viewport/viewport_camera.h"
#include "ferrum/math/mat4.h"
#include "ferrum/math/vec3.h"

/** Maximum per-object gizmos to render (stack allocation limit). */
#define PER_OBJECT_GIZMO_MAX 64

void scene_gizmo_per_object_draw(
    viewport_render_state_t *vp,
    const edit_entity_store_t *entities,
    const edit_selection_t *selection,
    gizmo_mode_t mode,
    transform_basis_t basis,
    const mat4_t *view,
    const mat4_t *proj,
    const vec3_t *eye_pos)
{
    if (!vp || !entities || !selection || !view || !proj || !eye_pos) return;
    if (edit_selection_count(selection) == 0) return;

    per_object_gizmo_t gizmos[PER_OBJECT_GIZMO_MAX];
    uint32_t count = per_object_gizmo_build(
        entities, selection, mode, basis,
        view, eye_pos, gizmos, PER_OBJECT_GIZMO_MAX);

    for (uint32_t i = 0; i < count; i++) {
        viewport_render_draw_gizmo(vp, &gizmos[i].gizmo,
                                     selection, view, proj);
    }
}
