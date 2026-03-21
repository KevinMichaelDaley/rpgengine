/**
 * @file scene_gizmo_bone_draw.c
 * @brief Per-bone gizmo rendering.
 *
 * Draws gizmos at each selected bone's world position using the
 * existing viewport_render_draw_gizmo() for each bone gizmo.
 *
 * Non-static functions (1 / 4-function rule):
 *   1. scene_gizmo_bone_draw
 */

#include "ferrum/editor/scene/scene_gizmo_bone.h"
#include "ferrum/editor/scene/scene_viewport_render.h"
#include "ferrum/editor/viewport/viewport_gizmo.h"
#include "ferrum/editor/edit_selection.h"
#include "ferrum/math/mat4.h"

void scene_gizmo_bone_draw(
    viewport_render_state_t *vp,
    const per_bone_gizmo_t *gizmos,
    uint32_t count,
    const mat4_t *view,
    const mat4_t *proj,
    const vec3_t *eye_pos,
    const edit_selection_t *selection)
{
    if (!vp || !gizmos || count == 0 || !view || !proj) return;
    if (!eye_pos) return;

    /* Bone gizmos don't depend on entity selection. If entity selection
     * is empty, create a stack-based dummy so viewport_render_draw_gizmo()
     * doesn't early-return (it only checks count > 0). */
    uint32_t dummy_id = 0;
    edit_selection_t dummy_sel = {
        .ids = &dummy_id, .count = 1, .capacity = 1, .version = 0
    };
    const edit_selection_t *sel = selection;
    if (!sel || edit_selection_count(sel) == 0) {
        sel = &dummy_sel;
    }

    for (uint32_t i = 0; i < count; i++) {
        /* Update arc quadrant signs so rotation rings render correctly. */
        gizmo_update_arc_quadrants(
            (gizmo_state_t *)&gizmos[i].gizmo, *eye_pos);
        viewport_render_draw_gizmo(vp, &gizmos[i].gizmo, sel,
                                   view, proj);
    }
}
