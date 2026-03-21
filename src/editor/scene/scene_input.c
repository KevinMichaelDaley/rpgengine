/**
 * @file scene_input.c
 * @brief SDL2 event dispatch for the scene editor.
 *
 * Handles window resize, quit, panel focus, divider drag, mouse state
 * tracking for Clay interaction, and keyboard shortcuts for entity
 * operations and panel toggles.
 */

#include "ferrum/editor/scene/scene_input.h"
#include "ferrum/editor/scene/scene_main.h"
#include "ferrum/editor/scene/scene_cmd.h"
#include "ferrum/editor/scene/scene_connection.h"
#include "ferrum/editor/scene/scene_sync.h"
#include "ferrum/editor/scene/viewport_bsp/viewport_bsp.h"
#include "ferrum/editor/scene/viewport_bsp/viewport_state.h"
#include "ferrum/editor/viewport/viewport_camera.h"
#include "ferrum/editor/viewport/viewport_nav.h"
#include "ferrum/editor/viewport/viewport_gizmo.h"
#include "ferrum/editor/viewport/transform_basis.h"
#include "ferrum/math/quat.h"
#include "ferrum/editor/viewport/selection_raycast.h"
#include "ferrum/editor/edit_selection.h"
#include "ferrum/editor/ctrl_cmd_defs.h"
#include "ferrum/editor/scene/snap_state.h"
#include "ferrum/editor/scene/cursor_place.h"
#include "ferrum/editor/ui/clay_theme.h"
#include "ferrum/editor/edit_entity_pivot.h"
#include "ferrum/editor/scene/scene_gizmo_per_object.h"
#include "ferrum/editor/scene/scene_gizmo_bone.h"
#include "ferrum/editor/edit_bone_selection.h"
#include "ferrum/editor/edit_skeleton_registry.h"
#include "ferrum/animation/constraint_params.h"
#include "ferrum/editor/viewport/snap/snap_surface_cast.h"
#include "ferrum/editor/viewport/snap/snap_surface_apply.h"
#include "ferrum/editor/viewport/snap/snap_depenetrate.h"
#include "ferrum/editor/edit_entity_matrix.h"
#include "ferrum/editor/scene/scene_viewport_render.h"
#include "ferrum/editor/viewport/bone_pick.h"
#include "ferrum/editor/edit_undo.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/undo_apply.h"
#include "ferrum/editor/scene/bone_pose/bone_pose_file.h"
#include "ferrum/editor/scene/scene_viewport_bone_overlay.h"
#include "ferrum/editor/scene/prefab/prefab_mode_enter.h"
#include "ferrum/editor/scene/prefab/prefab_def.h"
#include "ferrum/editor/scene/prefab/prefab_collect.h"
#include "ferrum/editor/scene/prefab/prefab_save.h"
#include "ferrum/editor/scene/scene_asset_load.h"

#include <SDL2/SDL.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* Camera orbit/pan sensitivity. */
#define CAMERA_ORBIT_SPEED 0.005f
#define CAMERA_PAN_SPEED   0.02f
#define CAMERA_ZOOM_SPEED  1.0f
#define CAMERA_FLY_SPEED   0.5f

/** Gizmo drag sensitivity: world units per pixel of mouse motion. */
#define GIZMO_DRAG_SPEED 0.01f

/** Rotation drag sensitivity: degrees per pixel of mouse motion. */
#define GIZMO_ROTATE_SPEED 0.5f

/* ---- Held-key tracking ---- */

/** @brief Check if a key is currently held (has had KEYDOWN without KEYUP). */
static bool key_is_held(const scene_ui_state_t *ui, uint32_t keycode) {
    for (int i = 0; i < ui->held_key_count; ++i) {
        if (ui->held_keys[i] == keycode) return true;
    }
    return false;
}

/** @brief Mark a key as held. Returns false if already held. */
static bool key_mark_held(scene_ui_state_t *ui, uint32_t keycode) {
    if (key_is_held(ui, keycode)) return false;
    if (ui->held_key_count < UI_HELD_KEYS_MAX) {
        ui->held_keys[ui->held_key_count++] = keycode;
    }
    return true;
}

/** @brief Mark a key as released (remove from held set). */
static void key_mark_released(scene_ui_state_t *ui, uint32_t keycode) {
    for (int i = 0; i < ui->held_key_count; ++i) {
        if (ui->held_keys[i] == keycode) {
            ui->held_keys[i] = ui->held_keys[--ui->held_key_count];
            return;
        }
    }
}

/* ---- Internal helpers ---- */

/** Target gizmo screen fraction: the gizmo axis tip will be this
 *  fraction of the viewport height away from center on screen. */
#define GIZMO_SCREEN_FRACTION 0.12f

/**
 * @brief Compute gizmo visual scale so the gizmo occupies a fixed
 *        fraction of screen height regardless of zoom and FOV.
 */
static float viewport_gizmo_screen_scale(const vec3_t *gizmo_pos,
                                           const vec3_t *eye_pos,
                                           float fov_y) {
    float dx = gizmo_pos->x - eye_pos->x;
    float dy = gizmo_pos->y - eye_pos->y;
    float dz = gizmo_pos->z - eye_pos->z;
    float dist = sqrtf(dx * dx + dy * dy + dz * dz);
    /* visible_height = 2 * dist * tan(fov/2)
     * gizmo should be GIZMO_SCREEN_FRACTION of that. */
    float scale = dist * tanf(fov_y * 0.5f) * GIZMO_SCREEN_FRACTION * 2.0f;
    if (scale < 0.3f) scale = 0.3f;
    return scale;
}

/**
 * @brief Try bone gizmo hit-test when per-object mode and bones selected.
 *
 * Builds per-bone gizmos for the active entity's skeleton, then runs
 * proximity-based pick.  On hit, starts a bone drag and returns true.
 *
 * @param ed          Scene editor.
 * @param fvp         Focused viewport state.
 * @param ray         World-space ray from cursor.
 * @param gizmo_scale Visual gizmo scale for hit radius.
 * @return true if a bone gizmo was hit and drag started.
 */
static bool try_bone_gizmo_pick(scene_editor_t *ed,
                                viewport_state_t *fvp,
                                const editor_ray_t *ray,
                                float gizmo_scale,
                                const mat4_t *vp_matrix,
                                float screen_x,
                                float screen_y) {
    if (edit_bone_selection_count(&ed->bone_selection) == 0) {
        return false;
    }
    uint32_t bone_eid = ed->bone_selection.entity_id;
    if (bone_eid == EDIT_BONE_SEL_NONE) {
        return false;
    }

    const edit_entity_t *ae = edit_entity_store_get(
        &ed->entities, bone_eid);
    if (!ae || !ae->active || ae->hidden) { return false; }

    /* Look up skeleton path attribute. */
    uint8_t at = 0, as = 0;
    const void *sp = entity_attrs_get(
        &ae->attrs, SCRIPT_KEY_SKEL_PATH, &at, &as);
    if (!sp || at != SCRIPT_ATTR_STR) { return false; }
    const char *spath = (const char *)sp;
    if (spath[0] == '\0') { return false; }

    /* Extract filename for registry lookup. */
    const char *fname = spath;
    for (const char *p = spath; *p; p++) {
        if (*p == '/') fname = p + 1;
    }
    const edit_skeleton_entry_t *entry =
        edit_skeleton_registry_get(&ed->skeleton_registry, fname);
    if (!entry) { return false; }

    /* Use per-entity pose override if available. */
    skeleton_def_t pick_skel_view;
    const skeleton_def_t *pick_skel = &entry->skel;
    const bone_pose_block_t *pick_bp =
        bone_pose_store_get(&ed->bone_poses, bone_eid);
    if (pick_bp) {
        pick_skel_view = entry->skel;
        pick_skel_view.rest_world = (mat4_t *)pick_bp->rest_world;
        pick_skel_view.tail_positions = (float *)pick_bp->tail_positions;
        pick_skel = &pick_skel_view;
    }

    /* Build model matrix and per-bone gizmo array. */
    mat4_t model = mat4_translation(ae->pos[0], ae->pos[1], ae->pos[2]);
    {
        mat4_t rot;
        quat_to_mat4(ae->orientation, &rot);
        mat4_t scale = mat4_scaling(ae->scale[0], ae->scale[1], ae->scale[2]);
        model = mat4_mul(model, mat4_mul(rot, scale));
    }

    per_bone_gizmo_t bone_gizmos[EDIT_BONE_SEL_MAX];
    uint32_t bg_count = per_bone_gizmo_build(
        pick_skel, &ed->bone_selection, &model,
        fvp->gizmo.mode, fvp->gizmo.basis,
        bone_gizmos, EDIT_BONE_SEL_MAX);
    if (bg_count == 0) { return false; }

    vec3_t eye = editor_camera_eye_position(&fvp->camera);
    int32_t best_idx = -1;
    gizmo_axis_t best_axis = GIZMO_AXIS_NONE;
    vec3_t hit_pos = {0, 0, 0};
    uint32_t hit_bone = UINT32_MAX;

    if (fvp->per_object_gizmo) {
        /* Per-bone gizmo mode: hit test each bone's individual gizmo. */
        /* Update arc quadrants for rotation ring hit testing. */
        for (uint32_t i = 0; i < bg_count; i++) {
            gizmo_update_arc_quadrants(
                (gizmo_state_t *)&bone_gizmos[i].gizmo, eye);
        }

        if (bg_count > 0 && bone_gizmos[0].gizmo.mode == GIZMO_MODE_ROTATE) {
            static const gizmo_axis_t ring_axes[3] = {
                GIZMO_AXIS_X, GIZMO_AXIS_Y, GIZMO_AXIS_Z
            };
            float global_best = 1e30f;
            int best_ring = -1;

            for (uint32_t gi = 0; gi < bg_count; gi++) {
                float scale_i = viewport_gizmo_screen_scale(
                    &bone_gizmos[gi].gizmo.position, &eye, fvp->camera.fov);
                float dists[3];
                gizmo_ring_screen_distances(&bone_gizmos[gi].gizmo, scale_i,
                                              vp_matrix, screen_x, screen_y,
                                              dists);
                for (int ri = 0; ri < 3; ri++) {
                    if (dists[ri] < global_best) {
                        global_best = dists[ri];
                        best_idx = (int32_t)gi;
                        best_ring = ri;
                    }
                }
            }
            if (best_ring >= 0 && global_best < GIZMO_RING_HIT_THRESHOLD) {
                best_axis = ring_axes[best_ring];
            } else {
                best_idx = -1;
            }
        } else {
            float best_dist = 1e30f;
            for (uint32_t i = 0; i < bg_count; i++) {
                float scale_i = viewport_gizmo_screen_scale(
                    &bone_gizmos[i].gizmo.position, &eye, fvp->camera.fov);

                gizmo_axis_t axis = gizmo_hit_test(
                    &bone_gizmos[i].gizmo, ray, scale_i,
                    vp_matrix, screen_x, screen_y);
                if (axis != GIZMO_AXIS_NONE) {
                    float dx = bone_gizmos[i].gizmo.position.x - eye.x;
                    float dy = bone_gizmos[i].gizmo.position.y - eye.y;
                    float dz = bone_gizmos[i].gizmo.position.z - eye.z;
                    float dist = dx*dx + dy*dy + dz*dz;
                    if (dist < best_dist) {
                        best_dist = dist;
                        best_idx = (int32_t)i;
                        best_axis = axis;
                    }
                }
            }
        }
        if (best_idx >= 0) {
            hit_pos = bone_gizmos[best_idx].gizmo.position;
            hit_bone = bone_gizmos[best_idx].bone_index;
        }
    } else {
        /* Single centroid gizmo: hit test the shared viewport gizmo
         * positioned at the centroid of selected bones. */
        vec3_t centroid = {0, 0, 0};
        for (uint32_t gi = 0; gi < bg_count; gi++) {
            centroid.x += bone_gizmos[gi].gizmo.position.x;
            centroid.y += bone_gizmos[gi].gizmo.position.y;
            centroid.z += bone_gizmos[gi].gizmo.position.z;
        }
        centroid.x /= (float)bg_count;
        centroid.y /= (float)bg_count;
        centroid.z /= (float)bg_count;
        fvp->gizmo.position = centroid;
        gizmo_update_arc_quadrants(&fvp->gizmo, eye);

        float gs = viewport_gizmo_screen_scale(
            &centroid, &eye, fvp->camera.fov);
        gizmo_axis_t axis = gizmo_hit_test(
            &fvp->gizmo, ray, gs, vp_matrix, screen_x, screen_y);
        if (axis != GIZMO_AXIS_NONE) {
            best_idx = 0; /* Use first bone as drag target. */
            best_axis = axis;
            hit_pos = centroid;
            hit_bone = bone_gizmos[0].bone_index;
        }
    }

    if (best_idx < 0) { return false; }

    /* Start bone drag. */
    fvp->bone_drag_index = hit_bone;
    fvp->gizmo.active_axis = best_axis;
    fvp->gizmo.dragging = true;
    fvp->gizmo.position = hit_pos;
    fvp->gizmo_drag_origin = hit_pos;

    /* Set gizmo orientation for correct rotation ring axes.
     * WORLD: identity. LOCAL: bone's world rotation. */
    if (fvp->gizmo.basis == TRANSFORM_BASIS_LOCAL && pick_skel) {
        mat4_t bone_em = edit_entity_build_model_matrix(ae);
        mat4_t combined_orient = mat4_mul(bone_em, pick_skel->rest_world[hit_bone]);
        combined_orient.m[12] = 0.0f;
        combined_orient.m[13] = 0.0f;
        combined_orient.m[14] = 0.0f;
        fvp->gizmo.orientation = combined_orient;
    } else {
        fvp->gizmo.orientation = mat4_identity();
    }
    fvp->gizmo_drag_accum = (vec3_t){0, 0, 0};
    fvp->gizmo_scale_accum = (vec3_t){1, 1, 1};
    fvp->gizmo_rot_accum = (quat_t){0, 0, 0, 1};
    fvp->snap_applied_delta = (vec3_t){0, 0, 0};
    fvp->snap_applied_scale = (vec3_t){1, 1, 1};
    fvp->snap_rot_accum_deg = 0.0f;
    fvp->snap_rot_applied_deg = 0.0f;
    fvp->snap_origin_pos = hit_pos;

    /* Save original rest_local for undo recording at drag end. */
    memcpy(fvp->bone_drag_orig_local, pick_skel->rest_local[hit_bone].m,
           sizeof(fvp->bone_drag_orig_local));

    return true;
}

/** Degrees to radians. */
static const float INPUT_DEG_TO_RAD = 3.14159265358979323846f / 180.0f;

/**
 * @brief Compute world-space AABB half-extents for a MESH entity from its
 *        cached snap mesh vertex positions.
 *
 * Scans all vertex positions to find the local-space bounding box, then
 * scales by the entity's scale.  If no snap mesh is cached, returns false
 * and the caller should use a generous fallback.
 *
 * @param cache      Snap mesh cache (non-NULL).
 * @param entity_id  Entity ID to look up.
 * @param scale      Entity scale (3 floats).
 * @param out_hw     Output half-width  (x).
 * @param out_hh     Output half-height (y).
 * @param out_hd     Output half-depth  (z).
 * @return true if snap mesh was found and AABB computed, false otherwise.
 */
static bool mesh_entity_half_extents_(const snap_mesh_cache_t *cache,
                                       uint32_t entity_id,
                                       const float scale[3],
                                       float *out_hw, float *out_hh,
                                       float *out_hd) {
    const snap_mesh_t *mesh = snap_mesh_cache_get(cache, entity_id);
    if (!mesh || !mesh->positions || mesh->vertex_count == 0) return false;

    /* Find local-space min/max from vertex positions. */
    float lmin[3] = { mesh->positions[0], mesh->positions[1],
                      mesh->positions[2] };
    float lmax[3] = { lmin[0], lmin[1], lmin[2] };
    for (uint32_t v = 1; v < mesh->vertex_count; ++v) {
        const float *p = &mesh->positions[v * 3];
        for (int a = 0; a < 3; ++a) {
            if (p[a] < lmin[a]) lmin[a] = p[a];
            if (p[a] > lmax[a]) lmax[a] = p[a];
        }
    }

    /* Half-extents in world space = local half-extents * scale.
     * We take the max of each axis's extent so the AABB covers the mesh
     * even if the local origin isn't centered. */
    float local_hw = (lmax[0] - lmin[0]) * 0.5f;
    float local_hh = (lmax[1] - lmin[1]) * 0.5f;
    float local_hd = (lmax[2] - lmin[2]) * 0.5f;

    /* Also offset the center: the local AABB center may not be at origin.
     * Account for this by expanding the half-extents to cover both sides. */
    float cx = (lmax[0] + lmin[0]) * 0.5f;
    float cy = (lmax[1] + lmin[1]) * 0.5f;
    float cz = (lmax[2] + lmin[2]) * 0.5f;
    float abs_cx = cx < 0.0f ? -cx : cx;
    float abs_cy = cy < 0.0f ? -cy : cy;
    float abs_cz = cz < 0.0f ? -cz : cz;

    *out_hw = (local_hw + abs_cx) * scale[0];
    *out_hh = (local_hh + abs_cy) * scale[1];
    *out_hd = (local_hd + abs_cz) * scale[2];
    return true;
}

/**
 * @brief Ensure an entity has a snap mesh (lazy primitive generation).
 */
static void ensure_entity_snap_mesh_(snap_mesh_cache_t *cache,
                                       uint32_t entity_id,
                                       uint32_t entity_type) {
    if (snap_mesh_cache_has(cache, entity_id)) return;
    switch (entity_type) {
    case EDIT_ENTITY_TYPE_BOX:
        snap_mesh_retain_box(cache, entity_id);
        break;
    case EDIT_ENTITY_TYPE_SPHERE:
        snap_mesh_retain_sphere(cache, entity_id);
        break;
    case EDIT_ENTITY_TYPE_CAPSULE:
        snap_mesh_retain_capsule(cache, entity_id);
        break;
    default:
        break;
    }
}

/** Maximum world-space environment triangles to collect for depenetration. */
#define SNAP_MAX_ENV_TRIS 2048

/**
 * @brief Collect world-space triangles from all visible non-selected entities.
 *
 * Transforms each environment entity's snap mesh triangles into world space
 * and appends them to the output buffer.
 *
 * @param ed           Scene editor.
 * @param out_verts    Output buffer (3 vec3_t per triangle, packed).
 * @param max_tris     Maximum triangles to collect.
 * @return Number of triangles collected.
 */
static uint32_t collect_env_triangles_(scene_editor_t *ed,
                                         vec3_t *out_verts,
                                         uint32_t max_tris) {
    uint32_t count = 0;
    snap_mesh_cache_t *cache = &ed->viewport.snap_meshes;

    for (uint32_t i = 0; i < ed->entities.capacity && count < max_tris; ++i) {
        if (edit_selection_contains(&ed->selection, i)) continue;
        const edit_entity_t *ent = edit_entity_store_get(&ed->entities, i);
        if (!ent || ent->pending_delete || ent->hidden) continue;

        /* Halfspaces are infinite planes — skip them here; they get
         * separate analytical depenetration in the iterative loop. */
        if (ent->type == EDIT_ENTITY_TYPE_HALFSPACE) continue;

        /* Ensure snap mesh exists for primitives. */
        ensure_entity_snap_mesh_(cache, i, ent->type);
        const snap_mesh_t *mesh = snap_mesh_cache_get(cache, i);
        if (!mesh || !mesh->positions) continue;

        mat4_t model = edit_entity_build_model_matrix(ent);
        uint32_t tri_count = mesh->index_count / 3;

        for (uint32_t t = 0; t < tri_count && count < max_tris; ++t) {
            for (int v = 0; v < 3; ++v) {
                uint32_t idx = mesh->indices[t * 3 + v];
                vec4_t local = {
                    mesh->positions[idx * 3 + 0],
                    mesh->positions[idx * 3 + 1],
                    mesh->positions[idx * 3 + 2],
                    1.0f
                };
                vec4_t world = mat4_mul_vec4(model, local);
                out_verts[count * 3 + v] = (vec3_t){world.x, world.y, world.z};
            }
            count++;
        }
    }
    return count;
}

/**
 * @brief Try surface snap: cast ray from cursor, snap entity to hit surface.
 *
 * Ctrl+drag = snap position to nearest surface point (face snap).
 * Ctrl+Shift+drag = snap to surface + push along normal until fully
 * depenetrated from ALL environment geometry.
 *
 * Excludes all selected entities from the raycast.
 *
 * @param ed          Scene editor.
 * @param shift_held  True if Shift is also held (surface offset mode).
 * @return true if surface snap was applied (false = no hit, let normal drag run).
 */
static bool try_surface_snap_(scene_editor_t *ed, bool shift_held) {
    viewport_state_t *fvp = scene_focused_vp(ed);

    /* Cast a ray from the current cursor position. */
    float sc = ed->clay_be.ui_scale;
    if (sc < 1.0f) sc = 1.0f;
    int lx = (int)(ed->ui.mouse_x / sc);
    int ly = (int)(ed->ui.mouse_y / sc);
    panel_rect_t vp_rect = fvp->rect;
    if (vp_rect.w <= 0 || vp_rect.h <= 0) return false;

    float nx = (float)(lx - vp_rect.x) / (float)vp_rect.w;
    float ny = 1.0f - (float)(ly - vp_rect.y) / (float)vp_rect.h;

    vec2_t screen_pos = {nx, ny};
    vec2_t vp_size = {(float)vp_rect.w, (float)vp_rect.h};
    editor_ray_t ray;
    if (editor_camera_screen_to_ray(&fvp->camera, screen_pos,
                                      vp_size, &ray) != 0) {
        return false;
    }

    /* Determine the entity being dragged. */
    uint32_t drag_id = UINT32_MAX;
    if (fvp->per_object_gizmo &&
        fvp->per_object_drag_entity != EDIT_ENTITY_INVALID_ID) {
        drag_id = fvp->per_object_drag_entity;
    } else if (edit_selection_count(&ed->selection) > 0) {
        drag_id = ed->active_object_id;
        if (drag_id == EDIT_ENTITY_INVALID_ID) {
            const uint32_t *sel = edit_selection_ids(&ed->selection);
            drag_id = sel[0];
        }
    }
    if (drag_id == UINT32_MAX) return false;

    /* Mark all selected entities as hidden for the raycast, then restore. */
    uint32_t sel_count = edit_selection_count(&ed->selection);
    const uint32_t *sel_ids = edit_selection_ids(&ed->selection);

    bool saved_hidden[256];
    uint32_t hide_count = (sel_count < 256) ? sel_count : 256;
    for (uint32_t i = 0; i < hide_count; ++i) {
        edit_entity_t *ent = edit_entity_store_get_mut(
            &ed->entities, sel_ids[i]);
        if (ent) {
            saved_hidden[i] = ent->hidden;
            ent->hidden = true;
        } else {
            saved_hidden[i] = false;
        }
    }

    /* Cast ray against scene. */
    snap_hit_t hit;
    snap_surface_cast_ray(ray.origin, ray.direction,
                            ed->entities.entities, ed->entities.capacity,
                            &ed->viewport.snap_meshes,
                            UINT32_MAX, &hit);

    /* Restore hidden state. */
    for (uint32_t i = 0; i < hide_count; ++i) {
        edit_entity_t *ent = edit_entity_store_get_mut(
            &ed->entities, sel_ids[i]);
        if (ent) ent->hidden = saved_hidden[i];
    }

    if (!hit.valid) return false;  /* No hit — let normal drag handle it. */

    edit_entity_t *ent = edit_entity_store_get_mut(&ed->entities, drag_id);
    if (!ent) return false;

    /* Resolve snap mode: Shift forces SURFACE, otherwise use configured mode. */
    snap_target_mode_t mode = shift_held ? SNAP_TARGET_SURFACE
                                         : ed->snap_target;

    if (mode == SNAP_TARGET_VERTEX) {
        /* Vertex snap: snap to nearest vertex of the hit triangle. */
        const snap_mesh_t *hit_mesh = snap_mesh_cache_get(
            &ed->viewport.snap_meshes, hit.entity_id);
        if (hit_mesh) {
            mat4_t hit_model = edit_entity_build_model_matrix(
                edit_entity_store_get(&ed->entities, hit.entity_id));
            snap_apply_vertex(ent, &hit, hit_mesh, &hit_model);
        } else {
            /* Fallback to face snap if no mesh data for hit entity. */
            snap_apply_face(ent, &hit);
        }
    } else if (mode == SNAP_TARGET_SURFACE) {
        /* Surface offset: orient to face normal, place at hit point,
         * then push outward until fully depenetrated from all env geo. */
        snap_apply_face(ent, &hit);

        /* Ensure the dragged entity has a snap mesh. */
        ensure_entity_snap_mesh_(&ed->viewport.snap_meshes, drag_id,
                                   ent->type);
        const snap_mesh_t *smesh = snap_mesh_cache_get(
            &ed->viewport.snap_meshes, drag_id);
        if (!smesh) return true;

        /* Step 1: initial push using the hit plane (fast, handles the
         * primary surface). */
        mat4_t model_b = edit_entity_build_model_matrix(ent);
        snap_depenetrate_result_t dep;
        if (snap_depenetrate_plane(smesh, &model_b,
                                     hit.position, hit.normal, &dep)) {
            ent->pos[0] += dep.push.x;
            ent->pos[1] += dep.push.y;
            ent->pos[2] += dep.push.z;
        }

        /* Step 2: collect all environment triangles and check for
         * remaining penetrations (handles edges, corners, adjacent
         * faces, multiple overlapping environment objects).
         * Static buffer — only one drag can be active at a time. */
        static vec3_t env_buf[SNAP_MAX_ENV_TRIS * 3];
        uint32_t env_count = collect_env_triangles_(
            ed, env_buf, SNAP_MAX_ENV_TRIS);

        /* Iterative push: resolve until fully depenetrated.
         * Each pass finds the deepest remaining penetration and pushes
         * along that face normal. At corners this may cascade (pushing
         * out of face A pushes into face B), so we iterate until clean.
         * Safety cap at 64 to avoid infinite loops from degenerate geo. */
        for (int iter = 0; iter < 64; ++iter) {
            bool pushed = false;

            /* Pass A: triangle mesh depenetration. */
            if (env_count > 0) {
                model_b = edit_entity_build_model_matrix(ent);
                snap_depenetrate_result_t tri_dep;
                if (snap_depenetrate_vs_tris(smesh, &model_b,
                                                env_buf, env_count,
                                                &tri_dep)) {
                    float nudge = 1.001f;
                    ent->pos[0] += tri_dep.push.x * nudge;
                    ent->pos[1] += tri_dep.push.y * nudge;
                    ent->pos[2] += tri_dep.push.z * nudge;
                    pushed = true;
                }
            }

            /* Pass B: halfspace plane depenetration.
             * Halfspaces are infinite planes — no mesh representation.
             * Check all non-selected HALFSPACE entities and push entity
             * above their plane using signed-distance depenetration. */
            model_b = edit_entity_build_model_matrix(ent);
            for (uint32_t hi = 0; hi < ed->entities.capacity; ++hi) {
                if (edit_selection_contains(&ed->selection, hi)) continue;
                const edit_entity_t *hent = edit_entity_store_get(
                    &ed->entities, hi);
                if (!hent || hent->pending_delete || hent->hidden) continue;
                if (hent->type != EDIT_ENTITY_TYPE_HALFSPACE) continue;

                /* Compute halfspace plane: normal = rotated +Y. */
                vec3_t up = {0.0f, 1.0f, 0.0f};
                vec3_t hs_normal = quat_rotate_vec3(hent->orientation, up);
                vec3_t hs_point = {hent->pos[0], hent->pos[1], hent->pos[2]};

                snap_depenetrate_result_t plane_dep;
                if (snap_depenetrate_plane(smesh, &model_b,
                                             hs_point, hs_normal,
                                             &plane_dep)) {
                    float nudge = 1.001f;
                    ent->pos[0] += plane_dep.push.x * nudge;
                    ent->pos[1] += plane_dep.push.y * nudge;
                    ent->pos[2] += plane_dep.push.z * nudge;
                    model_b = edit_entity_build_model_matrix(ent);
                    pushed = true;
                }
            }

            if (!pushed) break; /* Fully depenetrated. */
        }
    } else {
        /* Face snap: set position to hit point, orient to face normal. */
        snap_apply_face(ent, &hit);
    }

    return true;
}

/**
 * @brief Build a rotation matrix from a quaternion.
 *
 * Used for cursor-pivot orbit position computation.
 */
static mat4_t build_rotation_from_quat(quat_t dq) {
    mat4_t m;
    quat_to_mat4(dq, &m);
    return m;
}

/**
 * @brief Apply a rotation quaternion to all selected entities (optimistic).
 *
 * Composes the incremental quaternion with each entity's orientation.
 * In cursor basis, also orbits entity positions around the 3D cursor pivot.
 *
 * @param ed  Scene editor (non-NULL).
 * @param dq  Incremental rotation quaternion.
 */
static void apply_gizmo_rotate(scene_editor_t *ed, quat_t dq) {
    viewport_state_t *fvp = scene_focused_vp(ed);

    /* Per-bone gizmo drag: rotate the dragged bone's rest pose. */
    if (fvp->bone_drag_index != UINT32_MAX &&
        ed->bone_selection.entity_id != EDIT_BONE_SEL_NONE) {
        const edit_entity_t *ae = edit_entity_store_get(
            &ed->entities, ed->bone_selection.entity_id);
        if (ae && ae->active) {
            uint8_t at = 0, as = 0;
            const void *sp = entity_attrs_get(
                &ae->attrs, SCRIPT_KEY_SKEL_PATH, &at, &as);
            if (sp && at == SCRIPT_ATTR_STR) {
                const char *spath = (const char *)sp;
                const char *fname = spath;
                for (const char *p = spath; *p; p++) {
                    if (*p == '/') fname = p + 1;
                }
                edit_skeleton_entry_t *entry =
                    edit_skeleton_registry_get_mut(
                        &ed->skeleton_registry, fname);
                if (entry) {
                    /* Get the skeleton to operate on.  In prefab mode
                     * we mutate the shared registry skeleton directly.
                     * In regular mode we use per-entity overrides. */
                    skeleton_def_t *sk = &entry->skel;
                    skeleton_def_t pose_view;
                    uint32_t eid = ed->bone_selection.entity_id;
                    if (!ed->prefab_mode.active) {
                        bone_pose_block_t *pose =
                            bone_pose_store_get_mut(&ed->bone_poses, eid);
                        if (!pose) {
                            pose = bone_pose_store_ensure(
                                &ed->bone_poses, eid, sk);
                        }
                        if (pose) {
                            pose_view = *sk;
                            pose_view.rest_local = pose->rest_local;
                            pose_view.rest_world = pose->rest_world;
                            pose_view.tail_positions = pose->tail_positions;
                            sk = &pose_view;
                        }
                    }
                    /* Transform world-space rotation into parent-bone
                     * local space: local_dq = conj(parent_q) * dq * parent_q
                     * where parent_q = rotation of entity * parent_rest_world. */
                    uint32_t bidx = fvp->bone_drag_index;
                    mat4_t em = edit_entity_build_model_matrix(ae);
                    mat4_t combined;
                    uint32_t pidx = sk->parent_indices
                        ? sk->parent_indices[bidx] : UINT32_MAX;
                    if (pidx != UINT32_MAX && pidx < sk->joint_count) {
                        combined = mat4_mul(em, sk->rest_world[pidx]);
                    } else {
                        combined = em;
                    }
                    quat_t parent_q = quat_from_mat4(&combined);
                    quat_t conj_pq = quat_conjugate(parent_q);
                    /* local_dq = conj(parent_q) * dq * parent_q */
                    quat_t local_dq = quat_mul(quat_mul(conj_pq, dq), parent_q);
                    per_bone_gizmo_apply_rotate(
                        sk, bidx, local_dq);
                    if (ed->prefab_mode.active) {
                        ed->prefab_mode.dirty = true;
                    }
                    /* Don't bump dirty_gen: bone pose changes don't
                     * affect entity hierarchy (hull cache). */
                }
            }
        }
        return;
    }

    /* Per-object mode: rotate only the picked entity.
     * In cursor basis, pass the cursor as pivot so the entity orbits it. */
    if (fvp->per_object_gizmo &&
        fvp->per_object_drag_entity != EDIT_ENTITY_INVALID_ID) {
        const vec3_t *pivot = NULL;
        if (fvp->gizmo.basis == TRANSFORM_BASIS_CURSOR) {
            pivot = &fvp->cursor_3d;
        }
        per_object_gizmo_apply_rotate(&ed->entities,
            fvp->per_object_drag_entity, dq, pivot);
        return;
    }

    bool pivot_rotate = (fvp->gizmo.basis == TRANSFORM_BASIS_CURSOR);
    mat4_t rot_mat;
    vec3_t pivot;
    if (pivot_rotate) {
        rot_mat = build_rotation_from_quat(dq);
        pivot = fvp->cursor_3d;
    }

    for (uint32_t i = 0; i < ed->entities.capacity; ++i) {
        if (!edit_selection_contains(&ed->selection, i)) continue;
        edit_entity_t *ent = edit_entity_store_get_mut(&ed->entities, i);
        if (!ent) continue;

        /* Compose quaternion rotation. */
        ent->orientation = quat_normalize_safe(
            quat_mul(dq, ent->orientation), 1e-8f);

        /* Sync euler cache for display.  Canonicalize w >= 0 to avoid
         * branch jumps in the euler decomposition (q and -q are the
         * same rotation but decompose to different euler angles). */
        {
            quat_t cq = ent->orientation;
            if (cq.w < 0.0f) { cq.x = -cq.x; cq.y = -cq.y; cq.z = -cq.z; cq.w = -cq.w; }
            quat_to_euler_yxz(cq, &ent->rot[0], &ent->rot[1], &ent->rot[2]);
        }
        {
            float r2d = 180.0f / 3.14159265358979323846f;
            ent->rot[0] *= r2d;
            ent->rot[1] *= r2d;
            ent->rot[2] *= r2d;
        }

        /* In cursor basis, also orbit position around the cursor. */
        if (pivot_rotate) {
            float ox = ent->pos[0] - pivot.x;
            float oy = ent->pos[1] - pivot.y;
            float oz = ent->pos[2] - pivot.z;
            float nx = rot_mat.m[0] * ox + rot_mat.m[4] * oy
                      + rot_mat.m[8]  * oz;
            float ny = rot_mat.m[1] * ox + rot_mat.m[5] * oy
                      + rot_mat.m[9]  * oz;
            float nz = rot_mat.m[2] * ox + rot_mat.m[6] * oy
                      + rot_mat.m[10] * oz;
            ent->pos[0] = pivot.x + nx;
            ent->pos[1] = pivot.y + ny;
            ent->pos[2] = pivot.z + nz;
        }
    }
}

/**
 * @brief Snap all three euler axes on every selected entity.
 *
 * Called at rotation drag-end and when rotation snap is toggled ON,
 * NOT per-frame during drag (which causes euler-quat round-trip issues).
 */
static void snap_selected_euler_axes(scene_editor_t *ed) {
    if (!ed->snap.enabled[SNAP_ROTATION]) return;
    static const float R2D = 180.0f / 3.14159265358979323846f;
    static const float D2R = 3.14159265358979323846f / 180.0f;
    for (uint32_t i = 0; i < ed->entities.capacity; ++i) {
        if (!edit_selection_contains(&ed->selection, i)) continue;
        edit_entity_t *ent = edit_entity_store_get_mut(&ed->entities, i);
        if (!ent) continue;

        /* Decompose fresh from the authoritative quaternion.
         * Canonicalize to w >= 0 first so atan2 returns consistent
         * euler branches (q and -q are the same rotation). */
        quat_t cq = ent->orientation;
        if (cq.w < 0.0f) { cq.x = -cq.x; cq.y = -cq.y; cq.z = -cq.z; cq.w = -cq.w; }
        float ex, ey, ez;
        quat_to_euler_yxz(cq, &ex, &ey, &ez);
        ent->rot[0] = ex * R2D;
        ent->rot[1] = ey * R2D;
        ent->rot[2] = ez * R2D;

        ent->rot[0] = snap_state_quantize(&ed->snap, SNAP_ROTATION,
                                           ent->rot[0], 0);
        ent->rot[1] = snap_state_quantize(&ed->snap, SNAP_ROTATION,
                                           ent->rot[1], 1);
        ent->rot[2] = snap_state_quantize(&ed->snap, SNAP_ROTATION,
                                           ent->rot[2], 2);
        ent->orientation = quat_from_euler_yxz(
            ent->rot[0] * D2R, ent->rot[1] * D2R, ent->rot[2] * D2R);
    }

}

/**
 * @brief Apply gizmo drag delta to all selected entities (optimistic).
 *
 * For translate: moves entities by delta.
 * For scale: scales entities by (1 + delta component).
 * For rotate: use apply_gizmo_rotate() instead.
 */
static void apply_gizmo_drag(scene_editor_t *ed, vec3_t delta) {
    viewport_state_t *fvp = scene_focused_vp(ed);

    /* Per-bone gizmo drag: translate the dragged bone's rest pose. */
    if (fvp->bone_drag_index != UINT32_MAX &&
        ed->bone_selection.entity_id != EDIT_BONE_SEL_NONE) {
        const edit_entity_t *ae = edit_entity_store_get(
            &ed->entities, ed->bone_selection.entity_id);
        if (ae && ae->active) {
            uint8_t at = 0, as = 0;
            const void *sp = entity_attrs_get(
                &ae->attrs, SCRIPT_KEY_SKEL_PATH, &at, &as);
            if (sp && at == SCRIPT_ATTR_STR) {
                const char *spath = (const char *)sp;
                const char *fname = spath;
                for (const char *p = spath; *p; p++) {
                    if (*p == '/') fname = p + 1;
                }
                edit_skeleton_entry_t *entry =
                    edit_skeleton_registry_get_mut(
                        &ed->skeleton_registry, fname);
                if (entry) {
                    /* Get the skeleton to operate on.  In prefab mode
                     * we mutate the shared registry skeleton directly.
                     * In regular mode we use per-entity overrides. */
                    skeleton_def_t *sk = &entry->skel;
                    skeleton_def_t pose_view;
                    uint32_t eid = ed->bone_selection.entity_id;
                    if (!ed->prefab_mode.active) {
                        bone_pose_block_t *pose =
                            bone_pose_store_get_mut(&ed->bone_poses, eid);
                        if (!pose) {
                            pose = bone_pose_store_ensure(
                                &ed->bone_poses, eid, sk);
                        }
                        if (pose) {
                            pose_view = *sk;
                            pose_view.rest_local = pose->rest_local;
                            pose_view.rest_world = pose->rest_world;
                            pose_view.tail_positions = pose->tail_positions;
                            sk = &pose_view;
                        }
                    }
                    /* Transform world-space delta into parent-bone local space.
                     * rest_local is relative to the parent bone's rest_world
                     * (and the entity's model matrix).  We need:
                     *   local_delta = inv_rot(entity_model * parent_rest_world) * delta
                     * For rotation-only inverse, transpose the 3x3 part. */
                    uint32_t bidx = fvp->bone_drag_index;
                    mat4_t em = edit_entity_build_model_matrix(ae);
                    mat4_t combined;
                    uint32_t pidx = sk->parent_indices
                        ? sk->parent_indices[bidx] : UINT32_MAX;
                    if (pidx != UINT32_MAX && pidx < sk->joint_count) {
                        combined = mat4_mul(em, sk->rest_world[pidx]);
                    } else {
                        combined = em;
                    }
                    /* Inverse-rotate delta: transpose of 3x3. */
                    vec3_t local_delta;
                    local_delta.x = combined.m[0] * delta.x
                                  + combined.m[1] * delta.y
                                  + combined.m[2] * delta.z;
                    local_delta.y = combined.m[4] * delta.x
                                  + combined.m[5] * delta.y
                                  + combined.m[6] * delta.z;
                    local_delta.z = combined.m[8] * delta.x
                                  + combined.m[9] * delta.y
                                  + combined.m[10] * delta.z;
                    per_bone_gizmo_apply_drag(
                        sk, bidx, local_delta);
                    if (ed->prefab_mode.active) {
                        ed->prefab_mode.dirty = true;
                    }
                    /* Don't bump dirty_gen here: bone pose changes don't
                     * affect entity hierarchy, so hull cache doesn't need
                     * rebuilding.  dirty_gen drives hull rebuilds. */
                }
            }
        }
        return;
    }

    /* Per-object mode: apply drag to the single picked entity only. */
    if (fvp->per_object_gizmo &&
        fvp->per_object_drag_entity != EDIT_ENTITY_INVALID_ID) {
        per_object_gizmo_apply_drag(&ed->entities,
            fvp->per_object_drag_entity,
            fvp->gizmo.mode, delta);
        return;
    }

    /* In pivot edit mode, drag modifies pivot_offset in local space
     * instead of moving the entity position.  Also adjusts pos so
     * the geometry center stays fixed (same formula as cmd_pivot_id). */
    if (ed->ui.pivot_edit_mode &&
        fvp->gizmo.mode == GIZMO_MODE_TRANSLATE &&
        edit_selection_count(&ed->selection) == 1) {
        const uint32_t *sel_ids = edit_selection_ids(&ed->selection);
        edit_entity_t *ent = edit_entity_store_get_mut(
            &ed->entities, sel_ids[0]);
        if (ent) {
            /* Delta is world-space; inverse-rotate into local space. */
            quat_t inv = quat_conjugate(ent->orientation);
            vec3_t local_delta = quat_rotate_vec3(inv, delta);
            /* Compute world-space pos adjustment: R * S * dpivot. */
            vec3_t scaled = {
                local_delta.x * ent->scale[0],
                local_delta.y * ent->scale[1],
                local_delta.z * ent->scale[2],
            };
            vec3_t world_adj = quat_rotate_vec3(ent->orientation, scaled);
            ent->pivot_offset[0] += local_delta.x;
            ent->pivot_offset[1] += local_delta.y;
            ent->pivot_offset[2] += local_delta.z;
            ent->pos[0] += world_adj.x;
            ent->pos[1] += world_adj.y;
            ent->pos[2] += world_adj.z;
        }
        return;
    }

    for (uint32_t i = 0; i < ed->entities.capacity; ++i) {
        if (!edit_selection_contains(&ed->selection, i)) continue;
        edit_entity_t *ent = edit_entity_store_get_mut(&ed->entities, i);
        if (!ent) continue;

        switch (fvp->gizmo.mode) {
        case GIZMO_MODE_TRANSLATE:
            ent->pos[0] += delta.x;
            ent->pos[1] += delta.y;
            ent->pos[2] += delta.z;
            break;
        case GIZMO_MODE_SCALE:
            ent->scale[0] *= (1.0f + delta.x);
            ent->scale[1] *= (1.0f + delta.y);
            ent->scale[2] *= (1.0f + delta.z);
            break;
        default:
            break;
        }
    }
}

/**
 * @brief Send per-entity move_id commands after cursor-pivot rotation.
 *
 * Each entity was orbited around the cursor by a different amount
 * (different offsets from the cursor). The rotate command already handled
 * the euler angle change, so we send per-entity move_id commands
 * for the position deltas.
 */
static void send_pivot_move_commands(scene_editor_t *ed,
                                       quat_t total_rot_quat) {
    viewport_state_t *fvp = scene_focused_vp(ed);
    mat4_t rot = build_rotation_from_quat(total_rot_quat);
    vec3_t pivot = fvp->cursor_3d;

    for (uint32_t i = 0; i < ed->entities.capacity; ++i) {
        if (!edit_selection_contains(&ed->selection, i)) continue;
        const edit_entity_t *ent = edit_entity_store_get(&ed->entities, i);
        if (!ent) continue;

        /* The entity's current position is already orbited (from the
         * optimistic local update). Compute what position the server
         * thinks it's at (before orbit) to get the delta. The server
         * will have applied the rotate command (euler only), so its
         * position is: current_pos rotated back by total_rot_delta. */
        float cx = ent->pos[0] - pivot.x;
        float cy = ent->pos[1] - pivot.y;
        float cz = ent->pos[2] - pivot.z;
        /* Inverse rotation: transpose of rot (orthonormal). */
        float ox = rot.m[0] * cx + rot.m[1] * cy + rot.m[2]  * cz;
        float oy = rot.m[4] * cx + rot.m[5] * cy + rot.m[6]  * cz;
        float oz = rot.m[8] * cx + rot.m[9] * cy + rot.m[10] * cz;
        /* Delta = current - original. */
        float dx = cx - ox;
        float dy = cy - oy;
        float dz = cz - oz;

        if (fabsf(dx) < 1e-6f && fabsf(dy) < 1e-6f && fabsf(dz) < 1e-6f) {
            continue; /* Entity is at the pivot — no position change. */
        }

        char buf[256];
        uint32_t cid = scene_connection_next_id(&ed->connection);
        int n = snprintf(buf, sizeof(buf),
            "{\"id\":%u,\"cmd\":\"move_id\",\"args\":"
            "{\"entity_id\":%u,\"delta\":[%.6g,%.6g,%.6g]}}\n",
            (unsigned)cid, (unsigned)i,
            (double)dx, (double)dy, (double)dz);
        if (n > 0 && (size_t)n < sizeof(buf)) {
            scene_connection_send_cmd(&ed->connection, buf);
        }
    }
}

/**
 * @brief Send per-entity absolute transform commands via _id variants.
 *
 * Used when snap is enabled: each selected entity gets its own command
 * with the exact snapped value, avoiding the issue where a single
 * selection-wide command would set all entities to the same value.
 */
static void send_per_entity_abs_(scene_editor_t *ed, gizmo_mode_t mode) {
    char buf[256];
    for (uint32_t i = 0; i < ed->entities.capacity; ++i) {
        if (!edit_selection_contains(&ed->selection, i)) continue;
        const edit_entity_t *ent = edit_entity_store_get(&ed->entities, i);
        if (!ent) continue;

        uint32_t cid = scene_connection_next_id(&ed->connection);
        int n = 0;
        switch (mode) {
        case GIZMO_MODE_TRANSLATE:
            n = snprintf(buf, sizeof(buf),
                "{\"id\":%u,\"cmd\":\"move_id\",\"args\":"
                "{\"entity_id\":%u,\"abs\":[%.8g,%.8g,%.8g]}}\n",
                (unsigned)cid, (unsigned)i,
                (double)ent->pos[0], (double)ent->pos[1],
                (double)ent->pos[2]);
            break;
        case GIZMO_MODE_ROTATE:
            n = snprintf(buf, sizeof(buf),
                "{\"id\":%u,\"cmd\":\"rotate_id\",\"args\":"
                "{\"entity_id\":%u,\"abs\":[%.8g,%.8g,%.8g,%.8g]}}\n",
                (unsigned)cid, (unsigned)i,
                (double)ent->orientation.x, (double)ent->orientation.y,
                (double)ent->orientation.z, (double)ent->orientation.w);
            break;
        case GIZMO_MODE_SCALE:
            n = snprintf(buf, sizeof(buf),
                "{\"id\":%u,\"cmd\":\"scale_id\",\"args\":"
                "{\"entity_id\":%u,\"abs\":[%.8g,%.8g,%.8g]}}\n",
                (unsigned)cid, (unsigned)i,
                (double)ent->scale[0], (double)ent->scale[1],
                (double)ent->scale[2]);
            break;
        default:
            break;
        }
        if (n > 0 && (size_t)n < sizeof(buf)) {
            scene_connection_send_cmd(&ed->connection, buf);
        }
    }
}

/**
 * @brief Send a single server command for the accumulated gizmo drag.
 *
 * The server applies the transform to all currently selected entities.
 * For cursor-basis rotation, also sends per-entity move_id commands
 * to update positions orbited around the cursor pivot.
 */
static void send_gizmo_commands(scene_editor_t *ed, vec3_t total_delta) {
    viewport_state_t *fvp = scene_focused_vp(ed);

    /* Per-bone gizmo drag: bone rest-pose edits are local and do not
     * generate server commands (the skeleton is modified in-place).
     * Record a local bone undo entry with the original rest_local. */
    if (fvp->bone_drag_index != UINT32_MAX) {
        uint32_t bone_eid = ed->bone_selection.entity_id;
        uint32_t bone_idx = fvp->bone_drag_index;

        /* Determine current rest_local (after drag) for redo snapshot. */
        float new_local[16] = {0};
        {
            uint8_t bat = 0, bas = 0;
            const edit_entity_t *bae = edit_entity_store_get(
                &ed->entities, bone_eid);
            if (bae) {
                const void *bsp = entity_attrs_get(
                    &bae->attrs, SCRIPT_KEY_SKEL_PATH, &bat, &bas);
                if (bsp && bat == SCRIPT_ATTR_STR) {
                    const char *bs = (const char *)bsp;
                    const char *bfn = bs;
                    for (const char *bp = bs; *bp; bp++)
                        if (*bp == '/') bfn = bp + 1;
                    const edit_skeleton_entry_t *bse =
                        edit_skeleton_registry_get(&ed->skeleton_registry, bfn);
                    if (bse && bone_idx < bse->skel.joint_count) {
                        const skeleton_def_t *bsk = &bse->skel;
                        const bone_pose_block_t *bbp =
                            bone_pose_store_get(&ed->bone_poses, bone_eid);
                        if (bbp && !ed->prefab_mode.active) {
                            memcpy(new_local, bbp->rest_local[bone_idx].m,
                                   sizeof(new_local));
                        } else {
                            memcpy(new_local, bsk->rest_local[bone_idx].m,
                                   sizeof(new_local));
                        }
                    }
                }
            }
        }

        /* Record undo entry: snapshot = [original, new] rest_local
         * (2 × 16 floats = 128 bytes). Inverse restores original;
         * forward restores new. */
        float bone_snapshot[32];
        memcpy(bone_snapshot, fvp->bone_drag_orig_local, 64);
        memcpy(bone_snapshot + 16, new_local, 64);

        bool is_rotate = (fvp->gizmo.mode == GIZMO_MODE_ROTATE);
        edit_undo_entry_t bone_entry = {0};
        bone_entry.forward_type = is_rotate
            ? EDIT_CMD_TYPE_BONE_ROTATE : EDIT_CMD_TYPE_BONE_MOVE;
        bone_entry.inverse_type = bone_entry.forward_type;
        bone_entry.entity_id   = bone_eid;
        bone_entry.sub_index   = bone_idx;
        edit_undo_record(&ed->bone_undo, &bone_entry,
                          bone_snapshot, sizeof(bone_snapshot));

        fvp->bone_drag_index = UINT32_MAX;
        return;
    }

    /* Per-object mode: send command for the single dragged entity. */
    if (fvp->per_object_gizmo &&
        fvp->per_object_drag_entity != EDIT_ENTITY_INVALID_ID) {
        scene_per_object_send_commands(ed, total_delta);
        fvp->per_object_drag_entity = EDIT_ENTITY_INVALID_ID;
        return;
    }

    char cmd_buf[256];
    int cmd_len = 0;
    uint32_t cmd_id = scene_connection_next_id(&ed->connection);

    /* Pivot edit mode: send pivot_id with absolute pivot offset. */
    if (ed->ui.pivot_edit_mode &&
        fvp->gizmo.mode == GIZMO_MODE_TRANSLATE &&
        edit_selection_count(&ed->selection) == 1) {
        const uint32_t *sel_ids = edit_selection_ids(&ed->selection);
        const edit_entity_t *ent =
            edit_entity_store_get(&ed->entities, sel_ids[0]);
        if (ent) {
            int n = snprintf(cmd_buf, sizeof(cmd_buf),
                "{\"id\":%u,\"cmd\":\"pivot_id\",\"args\":{"
                "\"entity_id\":%u,"
                "\"pivot\":[%g,%g,%g]}}",
                cmd_id, sel_ids[0],
                (double)ent->pivot_offset[0],
                (double)ent->pivot_offset[1],
                (double)ent->pivot_offset[2]);
            if (n > 0 && (size_t)n < sizeof(cmd_buf)) {
                scene_connection_send_cmd(&ed->connection, cmd_buf);
                scene_sync_mark_sent(&ed->sync);
                scene_ui_tui_log_pending(&ed->ui, "pivot_id", cmd_id);
            }
        }
        return;
    }

    /* Compute the final snapped values — used for both sending and echo. */
    char echo[64];
    echo[0] = '\0';

    switch (fvp->gizmo.mode) {
    case GIZMO_MODE_TRANSLATE: {
        vec3_t snapped = snap_apply_position(&ed->snap,
            fvp->snap_origin_pos, fvp->gizmo_drag_accum);
        if (ed->snap.enabled[SNAP_POSITION]) {
            /* Send per-entity absolute position via move_id. */
            send_per_entity_abs_(ed, GIZMO_MODE_TRANSLATE);
        } else {
            float delta_arr[3] = {snapped.x, snapped.y, snapped.z};
            cmd_len = scene_cmd_format_move(cmd_buf, sizeof(cmd_buf),
                                             cmd_id, delta_arr);
        }
        snprintf(echo, sizeof(echo), "move [%.4g,%.4g,%.4g]",
                 (double)snapped.x, (double)snapped.y, (double)snapped.z);
        break;
    }
    case GIZMO_MODE_ROTATE: {
        if (ed->snap.enabled[SNAP_ROTATION]) {
            /* Send per-entity absolute orientation via rotate_id. */
            send_per_entity_abs_(ed, GIZMO_MODE_ROTATE);
        } else {
            quat_t rq = fvp->gizmo_rot_accum;
            float q[4] = {rq.x, rq.y, rq.z, rq.w};
            cmd_len = scene_cmd_format_rotate(cmd_buf, sizeof(cmd_buf),
                                               cmd_id, q);
        }
        float rx, ry, rz;
        { quat_t cq = fvp->gizmo_rot_accum;
          if (cq.w < 0.0f) { cq.x = -cq.x; cq.y = -cq.y; cq.z = -cq.z; cq.w = -cq.w; }
          quat_to_euler_yxz(cq, &rx, &ry, &rz); }
        float r2d = 180.0f / 3.14159265358979323846f;
        snprintf(echo, sizeof(echo), "rotate [%.4g,%.4g,%.4g]",
                 (double)(rx * r2d), (double)(ry * r2d),
                 (double)(rz * r2d));
        break;
    }
    case GIZMO_MODE_SCALE: {
        vec3_t snapped = snap_apply_scale(&ed->snap,
            fvp->snap_origin_scale, fvp->gizmo_scale_accum);
        if (ed->snap.enabled[SNAP_SCALE]) {
            /* Send per-entity absolute scale via scale_id. */
            send_per_entity_abs_(ed, GIZMO_MODE_SCALE);
        } else {
            float factor[3] = {snapped.x, snapped.y, snapped.z};
            cmd_len = scene_cmd_format_scale(cmd_buf, sizeof(cmd_buf),
                                              cmd_id, factor);
        }
        snprintf(echo, sizeof(echo), "scale [%.4g,%.4g,%.4g]",
                 (double)snapped.x, (double)snapped.y, (double)snapped.z);
        break;
    }
    default:
        break;
    }

    if (cmd_len > 0) {
        scene_connection_send_cmd(&ed->connection, cmd_buf);
        scene_sync_mark_sent(&ed->sync);
        if (echo[0] != '\0') {
            scene_ui_tui_log_pending(&ed->ui, echo, cmd_id);
        }
    }

    /* For cursor-basis rotation, send per-entity position updates. */
    if (fvp->gizmo.mode == GIZMO_MODE_ROTATE &&
        fvp->gizmo.basis == TRANSFORM_BASIS_CURSOR) {
        send_pivot_move_commands(ed, fvp->gizmo_rot_accum);
    }
}

/**
 * @brief Handle mouse button down: start divider drag, change focus,
 *        and update Clay mouse state.
 */
/**
 * @brief Check if a click at logical coords (lx, ly) hits a scrollbar track.
 *
 * Scrollbar tracks are 8px wide on the right edge of each panel's content
 * area. Returns the scrollbar ID (1=outliner, 2=inspector, 3=tui) or 0.
 */
static int scrollbar_hit_test(const scene_editor_t *ed, int lx, int ly) {
    /* Only panels with scrollable content. */
    static const panel_id_t panels[] = {PANEL_OUTLINER, PANEL_INSPECTOR, PANEL_TUI};
    static const int scroll_ids[] = {1, 2, 3};

    /* Check which panel needs a scrollbar and if click is in the track. */
    for (int p = 0; p < 3; p++) {
        if (!panel_layout_is_visible(&ed->layout, panels[p])) continue;

        panel_rect_t r = panel_layout_get_rect(&ed->layout, panels[p]);
        /* Scrollbar track: rightmost 8px of panel. */
        int track_left = r.x + r.w - 8 - THEME_PADDING_SMALL;
        int track_right = r.x + r.w - THEME_PADDING_SMALL;

        /* Outliner is split: top 60% entity list (ID 1),
         * bottom 40% asset browser (ID 4). */
        if (panels[p] == PANEL_OUTLINER) {
            int split_y = r.y + (r.h * 3) / 5;

            if (lx >= track_left && lx <= track_right) {
                if (ly >= split_y && ly <= r.y + r.h) {
                    /* Asset browser scrollbar (bottom 40%). */
                    bool has_sb = ed->ui.asset_browser_total
                                  > ed->ui.asset_browser_visible_lines;
                    if (has_sb) return 4;
                } else if (ly >= r.y + THEME_ROW_HEIGHT && ly < split_y) {
                    if (ed->prefab_mode.active) {
                        /* Prefab outliner scrollbar (top 60%, prefab mode). */
                        bool has_sb = ed->ui.prefab_outliner_total
                                      > ed->ui.prefab_outliner_visible_lines;
                        if (has_sb) return 5;
                    } else {
                        /* Outliner scrollbar (top 60%). */
                        bool has_sb = ed->ui.outliner_total
                                      > ed->ui.outliner_visible_lines;
                        if (has_sb) return 1;
                    }
                }
            }
            continue;
        }

        /* Check if this panel actually has a scrollbar. */
        bool has_scrollbar = false;
        if (panels[p] == PANEL_INSPECTOR) {
            has_scrollbar = ed->ui.inspector_total > ed->ui.inspector_visible_lines;
        } else if (panels[p] == PANEL_TUI) {
            has_scrollbar = ed->ui.tui_log_count > ed->ui.tui_log_visible;
        }
        if (!has_scrollbar) continue;

        int track_top = r.y + THEME_ROW_HEIGHT; /* below title bar */
        int track_bottom = r.y + r.h;

        if (lx >= track_left && lx <= track_right &&
            ly >= track_top && ly <= track_bottom) {
            return scroll_ids[p];
        }
    }
    return 0;
}

static bool handle_mouse_down(scene_editor_t *ed, const SDL_MouseButtonEvent *ev) {
    if (ev->button == SDL_BUTTON_LEFT) {
        ed->ui.mouse_down = true;
        ed->ui.mouse_clicked = true;

        /* Convert physical mouse coords to logical (layout) coords. */
        float sc = ed->clay_be.ui_scale;
        if (sc < 1.0f) sc = 1.0f;
        int lx = (int)((float)ev->x / sc);
        int ly = (int)((float)ev->y / sc);

        /* Check for divider drag start */
        divider_id_t div = panel_layout_divider_hit_test(&ed->layout,
                                                          lx, ly);
        if (div != DIVIDER_NONE) {
            ed->dragging_divider = div;
            return true;
        }

        /* Check for BSP divider drag start (within the viewport panel). */
        {
            panel_rect_t vp_panel = panel_layout_get_rect(&ed->layout,
                                                           PANEL_VIEWPORT);
            uint8_t bsp_node;
            if (viewport_bsp_hit_test_divider(&ed->vp_bsp, &vp_panel,
                                               lx, ly, &bsp_node)) {
                ed->dragging_bsp_node = (int8_t)bsp_node;
                return true;
            }
        }

        /* Check for scrollbar drag start. */
        int sb = scrollbar_hit_test(ed, lx, ly);
        if (sb > 0) {
            ed->ui.scrollbar_dragging = sb;
            ed->ui.scrollbar_drag_y = (float)ly;
            if (sb == 1) ed->ui.scrollbar_drag_scroll = ed->ui.outliner_scroll;
            else if (sb == 2) ed->ui.scrollbar_drag_scroll = ed->ui.inspector_scroll;
            else if (sb == 3) ed->ui.scrollbar_drag_scroll = ed->ui.tui_log_scroll;
            else if (sb == 4) ed->ui.scrollbar_drag_scroll = ed->ui.asset_browser_scroll;
            else if (sb == 5) ed->ui.scrollbar_drag_scroll = ed->ui.prefab_outliner_scroll;
            return true;
        }

        /* Click-to-focus */
        panel_id_t hit = panel_layout_hit_test(&ed->layout, lx, ly);
        panel_layout_set_focus(&ed->layout, hit);

        /* Viewport left-click: gizmo interaction or entity selection.
         * Skip if click is in the toolbar overlay area (title + mode bar)
         * at the top of the viewport — those clicks belong to Clay UI. */
        if (hit == PANEL_VIEWPORT) {
            /* Compute fresh BSP rects and determine which viewport
             * was clicked (rects may not be up-to-date yet this frame). */
            panel_rect_t bsp_rects[VIEWPORT_MAX_COUNT];
            {
                panel_rect_t vp_panel = panel_layout_get_rect(&ed->layout,
                                                               PANEL_VIEWPORT);
                memset(bsp_rects, 0, sizeof(bsp_rects));
                viewport_bsp_compute_rects(&ed->vp_bsp, &vp_panel, bsp_rects);
                /* Store computed rects into viewport states. */
                for (int vi = 0; vi < VIEWPORT_MAX_COUNT; vi++) {
                    ed->viewports[vi].rect = bsp_rects[vi];
                }
                uint8_t hit_vp;
                if (viewport_bsp_hit_test_viewport(&ed->vp_bsp, bsp_rects,
                                                    lx, ly, &hit_vp)) {
                    if (hit_vp != ed->vp_bsp.focused_viewport) {
                        ed->prev_focused_vp = ed->vp_bsp.focused_viewport;
                    }
                    ed->vp_bsp.focused_viewport = hit_vp;
                }
            }

            viewport_state_t *fvp = scene_focused_vp(ed);
            panel_rect_t vp_rect = fvp->rect;
            int toolbar_h = THEME_ROW_HEIGHT * 2 + THEME_PADDING;
            if (ly < vp_rect.y + toolbar_h) {
                /* Click is in the toolbar area.  Handle mode buttons
                 * directly so they behave identically to keybindings
                 * (no selection clear, proper toggle to NONE). */
                int row2_top = vp_rect.y + THEME_ROW_HEIGHT;
                if (ly >= row2_top) {
                    /* Second row: mode button + separator + basis button.
                     * Approximate hit zones for the two cycling buttons. */
                    int rel_x = lx - vp_rect.x;
                    int btn_start = THEME_PADDING_SMALL;
                    int mode_w = 70;
                    int gap = THEME_PADDING_SMALL;
                    int sep_w = 1 + 2 * gap; /* separator + gaps */
                    int basis_start = btn_start + mode_w + sep_w;
                    int basis_w = 80;
                    if (rel_x >= btn_start &&
                        rel_x < btn_start + mode_w) {
                        /* Mode cycle: same as '.' hotkey. */
                        uint8_t next = (ed->ui.transform_mode + 1)
                                       % UI_MODE_COUNT;
                        ed->ui.transform_mode = next;
                        switch (next) {
                        case UI_MODE_TRANSLATE:
                            gizmo_state_set_mode(&fvp->gizmo,
                                GIZMO_MODE_TRANSLATE); break;
                        case UI_MODE_ROTATE:
                            gizmo_state_set_mode(&fvp->gizmo,
                                GIZMO_MODE_ROTATE); break;
                        case UI_MODE_SCALE:
                            gizmo_state_set_mode(&fvp->gizmo,
                                GIZMO_MODE_SCALE); break;
                        default:
                            gizmo_state_set_mode(&fvp->gizmo,
                                GIZMO_MODE_NONE); break;
                        }
                    } else if (rel_x >= basis_start &&
                               rel_x < basis_start + basis_w) {
                        fvp->gizmo.basis = transform_basis_next(
                            fvp->gizmo.basis);
                        char msg[64];
                        snprintf(msg, sizeof(msg), "Basis: %s",
                                 transform_basis_name(fvp->gizmo.basis));
                        scene_ui_tui_log(&ed->ui, msg);
                    }
                }
                return false;
            }
            float nx = (float)(lx - vp_rect.x) / (float)vp_rect.w;
            float ny = (float)(ly - vp_rect.y) / (float)vp_rect.h;
            /* Screen coords for 2D gizmo test (0,0 = top-left). */
            float screen_nx = nx;
            float screen_ny = ny;
            /* FBO is displayed Y-flipped by Clay's ortho projection,
             * so screen top = FBO bottom.  Flip ny so the ray matches
             * the visual scene rather than the raw FBO layout. */
            ny = 1.0f - ny;

            vec2_t screen_pos = {nx, ny};
            vec2_t vp_size = {(float)vp_rect.w, (float)vp_rect.h};
            editor_ray_t ray;
            if (editor_camera_screen_to_ray(&fvp->camera,
                                              screen_pos, vp_size, &ray) == 0) {
                /* Test gizmo hit first (if we have a selection). */
                bool gizmo_hit = false;
                if (edit_selection_count(&ed->selection) > 0) {
                    vec3_t eye = editor_camera_eye_position(
                        &fvp->camera);
                    float gscale = viewport_gizmo_screen_scale(
                        &fvp->gizmo.position, &eye,
                        fvp->camera.fov);
                    /* Build VP matrix for screen-space ring test. */
                    float aspect = (vp_rect.w > 0 && vp_rect.h > 0)
                        ? (float)vp_rect.w / (float)vp_rect.h : 1.0f;
                    mat4_t view_m, proj_m;
                    editor_camera_view_matrix(&fvp->camera, &view_m);
                    editor_camera_projection_matrix(&fvp->camera,
                                                      aspect, &proj_m);
                    mat4_t vp_m = mat4_mul(proj_m, view_m);

                    /* When bones are selected, ALL gizmo interaction
                     * targets bones — skip entity gizmos entirely. */
                    fvp->bone_drag_index = UINT32_MAX;
                    bool bones_active = edit_bone_selection_count(
                        &ed->bone_selection) > 0;

                    if (bones_active) {
                        gizmo_hit = try_bone_gizmo_pick(
                            ed, fvp, &ray, gscale,
                            &vp_m, screen_nx, screen_ny);
                    } else if (fvp->per_object_gizmo) {
                        /* Per-object mode: hit test entity gizmos. */
                        gizmo_hit = scene_per_object_gizmo_hit_test(
                            ed, &ray, gscale, &vp_m,
                            screen_nx, screen_ny);
                    } else {
                    gizmo_axis_t axis = gizmo_hit_test(
                        &fvp->gizmo, &ray, gscale,
                        &vp_m, screen_nx, screen_ny);
                    if (axis != GIZMO_AXIS_NONE) {
                        fvp->gizmo.active_axis = axis;
                        fvp->gizmo.dragging = true;
                        fvp->gizmo_drag_origin = fvp->gizmo.position;
                        fvp->gizmo_drag_accum = (vec3_t){0, 0, 0};
                        fvp->gizmo_scale_accum = (vec3_t){1, 1, 1};
                        fvp->gizmo_rot_accum = (quat_t){0, 0, 0, 1};
                        fvp->snap_applied_delta = (vec3_t){0, 0, 0};
                        fvp->snap_applied_scale = (vec3_t){1, 1, 1};
                        fvp->snap_rot_accum_deg = 0.0f;
                        fvp->snap_rot_applied_deg = 0.0f;
                        fvp->snap_rot_origin[0] = 0.0f;
                        fvp->snap_rot_origin[1] = 0.0f;
                        fvp->snap_rot_origin[2] = 0.0f;
                        if (ed->active_object_id != EDIT_ENTITY_INVALID_ID) {
                            const edit_entity_t *re = edit_entity_store_get(
                                &ed->entities, ed->active_object_id);
                            if (re) {
                                fvp->snap_rot_origin[0] = re->rot[0];
                                fvp->snap_rot_origin[1] = re->rot[1];
                                fvp->snap_rot_origin[2] = re->rot[2];
                            }
                        }
                        /* Capture active entity's pos/scale for snap reference. */
                        fvp->snap_origin_pos = fvp->gizmo.position;
                        fvp->snap_origin_scale = (vec3_t){1, 1, 1};
                        if (ed->active_object_id != EDIT_ENTITY_INVALID_ID) {
                            const edit_entity_t *ae = edit_entity_store_get(
                                &ed->entities, ed->active_object_id);
                            if (ae) {
                                fvp->snap_origin_pos = (vec3_t){
                                    ae->pos[0], ae->pos[1], ae->pos[2]};
                                fvp->snap_origin_scale = (vec3_t){
                                    ae->scale[0], ae->scale[1], ae->scale[2]};
                            }
                        }
                        gizmo_hit = true;
                    }
                    } /* end unified gizmo branch */
                }

                /* If no gizmo hit, try bone picking on any entity with
                 * a visible skeleton, before falling back to entity pick. */
                bool bone_picked = false;
                if (!gizmo_hit) {
                    /* Scan all visible entities with skeletons. */
                    uint32_t ecap = ed->entities.capacity;
                    for (uint32_t ei = 0;
                         ei < ecap && !bone_picked; ei++) {
                        const edit_entity_t *ae =
                            edit_entity_store_get(&ed->entities, ei);
                        if (!ae || !ae->active || ae->hidden) continue;
                        uint8_t bat = 0, bas = 0;
                        const void *bsp = entity_attrs_get(
                            &ae->attrs, SCRIPT_KEY_SKEL_PATH, &bat, &bas);
                        if (!bsp || bat != SCRIPT_ATTR_STR) continue;
                        const char *bspath = (const char *)bsp;
                        if (bspath[0] == '\0') continue;
                        const char *bfn = bspath;
                        for (const char *bp = bspath; *bp; bp++) {
                            if (*bp == '/') bfn = bp + 1;
                        }
                        const edit_skeleton_entry_t *bse =
                            edit_skeleton_registry_get(
                                &ed->skeleton_registry, bfn);
                        if (!bse || bse->skel.joint_count == 0 ||
                            !bse->skel.tail_positions) continue;

                        const skeleton_def_t *sk = &bse->skel;
                        /* Use per-entity pose override if available. */
                        const mat4_t *pick_rw = sk->rest_world;
                        const float *pick_tp = sk->tail_positions;
                        const bone_pose_block_t *pick_bpb =
                            bone_pose_store_get(&ed->bone_poses, ei);
                        if (pick_bpb) {
                            pick_rw = pick_bpb->rest_world;
                            pick_tp = pick_bpb->tail_positions;
                        }
                        uint32_t bcand_n = sk->joint_count;
                        if (bcand_n > 256) bcand_n = 256;
                        bone_pick_candidate_t bcands[256];
                        for (uint32_t bj = 0; bj < bcand_n; bj++) {
                            bcands[bj].bone_index = bj;
                            bcands[bj].cap_a = (vec3_t){
                                pick_rw[bj].m[12] + ae->pos[0],
                                pick_rw[bj].m[13] + ae->pos[1],
                                pick_rw[bj].m[14] + ae->pos[2]};
                            bcands[bj].cap_b = (vec3_t){
                                pick_tp[bj * 3 + 0] + ae->pos[0],
                                pick_tp[bj * 3 + 1] + ae->pos[1],
                                pick_tp[bj * 3 + 2] + ae->pos[2]};
                            bone_capsule_params_t bcp;
                            float bh[3] = {pick_rw[bj].m[12],
                                           pick_rw[bj].m[13],
                                           pick_rw[bj].m[14]};
                            float bt[3] = {pick_tp[bj*3],
                                           pick_tp[bj*3+1],
                                           pick_tp[bj*3+2]};
                            bone_capsule_params_from_joint(bh, bt, &bcp);
                            bcands[bj].radius = bcp.radius;
                        }
                        uint32_t picked_bone;
                        if (pick_nearest_bone(&ray, bcands, bcand_n,
                                              &picked_bone)) {
                            SDL_Keymod bmod = SDL_GetModState();
                            if (bmod & KMOD_SHIFT) {
                                edit_bone_selection_toggle(
                                    &ed->bone_selection,
                                    ei, picked_bone);
                            } else {
                                edit_bone_selection_clear(
                                    &ed->bone_selection);
                                edit_bone_selection_add(
                                    &ed->bone_selection,
                                    ei, picked_bone);
                            }
                            bone_picked = true;
                        }
                    }
                }

                /* If no gizmo hit and no bone picked, do entity picking. */
                if (!gizmo_hit && !bone_picked) {
                    uint32_t cap = ed->entities.capacity;
                    uint32_t count = 0;
                    pick_candidate_t candidates[256];
                    for (uint32_t i = 0; i < cap && count < 256; ++i) {
                        const edit_entity_t *ent =
                            edit_entity_store_get(&ed->entities, i);
                        if (!ent || ent->pending_delete || ent->hidden)
                            continue;
                        float gc[3];
                        edit_entity_geometry_center(ent, gc);
                        float hw, hh, hd;
                        if (ent->type == EDIT_ENTITY_TYPE_MESH) {
                            /* Use actual mesh AABB for picking accuracy. */
                            if (!mesh_entity_half_extents_(
                                    &ed->viewport.snap_meshes, i,
                                    ent->scale, &hw, &hh, &hd)) {
                                /* No cached mesh yet; use generous fallback. */
                                hw = ent->scale[0] * 2.0f;
                                hh = ent->scale[1] * 2.0f;
                                hd = ent->scale[2] * 2.0f;
                            }
                        } else {
                            hw = ent->scale[0] * 0.5f;
                            hh = ent->scale[1] * 0.5f;
                            hd = ent->scale[2] * 0.5f;
                        }
                        candidates[count].entity_id = i;
                        candidates[count].aabb_min = (vec3_t){
                            gc[0] - hw, gc[1] - hh, gc[2] - hd};
                        candidates[count].aabb_max = (vec3_t){
                            gc[0] + hw, gc[1] + hh, gc[2] + hd};
                        count++;
                    }

                    uint32_t picked_id;
                    SDL_Keymod mod = SDL_GetModState();
                    if (pick_nearest_entity(&ray, candidates, count,
                                             &picked_id)) {
                        if (mod & KMOD_SHIFT) {
                            /* Shift-click: toggle selection. */
                            if (edit_selection_contains(&ed->selection,
                                                        picked_id)) {
                                ed->ui.action = UI_ACTION_DESELECT_ENTITY;
                            } else {
                                ed->ui.action = UI_ACTION_SELECT_ENTITY;
                            }
                            ed->ui.action_target = picked_id;
                            ed->active_object_id = picked_id;
                        } else if (fvp->gizmo.mode == GIZMO_MODE_TRANSLATE &&
                                   edit_selection_contains(&ed->selection,
                                                            picked_id)) {
                            /* Already-selected entity in translate mode:
                             * start free-move on camera-facing plane. */
                            fvp->gizmo.dragging = true;
                            fvp->free_dragging = true;
                            fvp->gizmo_drag_origin = fvp->gizmo.position;
                            fvp->gizmo_drag_accum = (vec3_t){0, 0, 0};
                            fvp->gizmo_scale_accum = (vec3_t){1, 1, 1};
                            fvp->gizmo_rot_accum = (quat_t){0, 0, 0, 1};
                            fvp->snap_applied_delta = (vec3_t){0, 0, 0};
                            fvp->snap_applied_scale = (vec3_t){1, 1, 1};
                            fvp->snap_origin_pos = fvp->gizmo.position;
                            fvp->snap_origin_scale = (vec3_t){1, 1, 1};
                            if (ed->active_object_id != EDIT_ENTITY_INVALID_ID) {
                                const edit_entity_t *ae = edit_entity_store_get(
                                    &ed->entities, ed->active_object_id);
                                if (ae) {
                                    fvp->snap_origin_pos = (vec3_t){
                                        ae->pos[0], ae->pos[1], ae->pos[2]};
                                }
                            }
                        } else {
                            edit_selection_clear(&ed->selection);
                            edit_bone_selection_clear(&ed->bone_selection);
                            ed->ui.pivot_edit_mode = false;
                            ed->ui.action = UI_ACTION_SELECT_ENTITY;
                            ed->ui.action_target = picked_id;
                            ed->active_object_id = picked_id;
                        }
                    } else {
                        /* No entity hit — start box select drag.
                         * Store logical coords (divide by UI scale) so
                         * they match panel_layout_get_rect() coordinates. */
                        fvp->box_selecting = true;
                        float bsc = ed->clay_be.ui_scale;
                        if (bsc < 1.0f) bsc = 1.0f;
                        fvp->box_select_start_x = ed->ui.mouse_x / bsc;
                        fvp->box_select_start_y = ed->ui.mouse_y / bsc;
                        if (!(mod & KMOD_SHIFT)) {
                            edit_selection_clear(&ed->selection);
                            /* Bone selection is independent — don't clear
                             * it on background click. */
                        }
                    }
                }
            }
        }
    } else if (ev->button == SDL_BUTTON_MIDDLE) {
        ed->ui.middle_mouse_down = true;
    } else if (ev->button == SDL_BUTTON_RIGHT) {
        SDL_Keymod mod = SDL_GetModState();
        if (mod & KMOD_CTRL) {
            /* Ctrl+right-click: place 3D cursor via raycast. */
            float sc = ed->clay_be.ui_scale;
            if (sc < 1.0f) sc = 1.0f;
            int lx = (int)((float)ev->x / sc);
            int ly = (int)((float)ev->y / sc);

            panel_id_t hit = panel_layout_hit_test(&ed->layout, lx, ly);
            if (hit == PANEL_VIEWPORT) {
                viewport_state_t *fvp = scene_focused_vp(ed);
                panel_rect_t vp_rect = fvp->rect;
                float nx = (float)(lx - vp_rect.x) / (float)vp_rect.w;
                float ny = (float)(ly - vp_rect.y) / (float)vp_rect.h;
                /* FBO is displayed Y-flipped by Clay's ortho projection,
                 * so screen top = FBO bottom.  Flip ny so the ray matches
                 * the visual scene rather than the raw FBO layout. */
                ny = 1.0f - ny;

                vec2_t screen_pos = {nx, ny};
                vec2_t vp_size = {(float)vp_rect.w, (float)vp_rect.h};
                editor_ray_t ray;
                if (editor_camera_screen_to_ray(&fvp->camera,
                                                  screen_pos, vp_size,
                                                  &ray) == 0) {
                    /* Try entity hit first. */
                    bool placed = false;
                    uint32_t cap = ed->entities.capacity;
                    pick_candidate_t candidates[256];
                    uint32_t count = 0;
                    for (uint32_t i = 0; i < cap && count < 256; ++i) {
                        const edit_entity_t *ent =
                            edit_entity_store_get(&ed->entities, i);
                        if (!ent || ent->pending_delete || ent->hidden)
                            continue;
                        float gc[3];
                        edit_entity_geometry_center(ent, gc);
                        float hw, hh, hd;
                        if (ent->type == EDIT_ENTITY_TYPE_MESH) {
                            if (!mesh_entity_half_extents_(
                                    &ed->viewport.snap_meshes, i,
                                    ent->scale, &hw, &hh, &hd)) {
                                hw = ent->scale[0] * 2.0f;
                                hh = ent->scale[1] * 2.0f;
                                hd = ent->scale[2] * 2.0f;
                            }
                        } else {
                            hw = ent->scale[0] * 0.5f;
                            hh = ent->scale[1] * 0.5f;
                            hd = ent->scale[2] * 0.5f;
                        }
                        candidates[count].entity_id = i;
                        candidates[count].aabb_min = (vec3_t){
                            gc[0] - hw, gc[1] - hh, gc[2] - hd};
                        candidates[count].aabb_max = (vec3_t){
                            gc[0] + hw, gc[1] + hh, gc[2] + hd};
                        count++;
                    }

                    uint32_t picked_id;
                    if (pick_nearest_entity(&ray, candidates, count,
                                             &picked_id)) {
                        const edit_entity_t *ent =
                            edit_entity_store_get(&ed->entities, picked_id);
                        if (ent) {
                            fvp->cursor_3d = (vec3_t){
                                ent->pos[0], ent->pos[1], ent->pos[2]};
                            fvp->cursor_orientation = ent->orientation;
                            placed = true;
                        }
                    }

                    if (!placed) {
                        /* No entity hit -- intersect Y=0 ground plane. */
                        float t_hit;
                        vec3_t hit_pt;
                        if (cursor_ray_plane_intersect(ray.origin,
                                                        ray.direction,
                                                        0.0f, &t_hit,
                                                        &hit_pt)) {
                            fvp->cursor_3d = hit_pt;
                            fvp->cursor_orientation = (quat_t){0, 0, 0, 1};
                        }
                    }

                    char msg[64];
                    snprintf(msg, sizeof(msg),
                             "Cursor: (%.2f, %.2f, %.2f)",
                             (double)fvp->cursor_3d.x,
                             (double)fvp->cursor_3d.y,
                             (double)fvp->cursor_3d.z);
                    scene_ui_tui_log(&ed->ui, msg);
                }
            }
            return true;
        }
        /* Non-Ctrl right-click: track for middle mouse pan alternative. */
        ed->ui.right_mouse_down = true;
    }
    return false; /* let Clay also handle the click */
}

/**
 * @brief Complete a box select: project entity centers to screen, select
 *        those inside the drag rectangle.
 *
 * Uses the VP matrix to project world positions to normalized viewport
 * coords [0,1]. Entities whose projected center falls inside the box
 * are added to the selection.
 */
static void finish_box_select_(scene_editor_t *ed) {
    viewport_state_t *fvp = scene_focused_vp(ed);
    panel_rect_t vp_rect = fvp->rect;
    if (vp_rect.w <= 0 || vp_rect.h <= 0) return;

    /* Convert current mouse to logical coords (start was already stored
     * in logical coords). Panel rects are in logical pixels. */
    float bsc = ed->clay_be.ui_scale;
    if (bsc < 1.0f) bsc = 1.0f;
    float cur_lx = ed->ui.mouse_x / bsc;
    float cur_ly = ed->ui.mouse_y / bsc;

    /* Normalized box corners in viewport space [0,1].
     * FBO is displayed Y-flipped, so flip sy so projected entity
     * coords (which use standard NDC→screen mapping) match. */
    float sx0 = (fvp->box_select_start_x - (float)vp_rect.x) / (float)vp_rect.w;
    float sy0 = 1.0f - (fvp->box_select_start_y - (float)vp_rect.y) / (float)vp_rect.h;
    float sx1 = (cur_lx - (float)vp_rect.x) / (float)vp_rect.w;
    float sy1 = 1.0f - (cur_ly - (float)vp_rect.y) / (float)vp_rect.h;

    /* Ensure min/max ordering. */
    float bx0 = sx0 < sx1 ? sx0 : sx1;
    float by0 = sy0 < sy1 ? sy0 : sy1;
    float bx1 = sx0 < sx1 ? sx1 : sx0;
    float by1 = sy0 < sy1 ? sy1 : sy0;

    /* Skip tiny drags (likely just a click). */
    if ((bx1 - bx0) < 0.01f && (by1 - by0) < 0.01f) return;

    /* Build view-projection matrix. */
    float aspect = (float)vp_rect.w / (float)vp_rect.h;
    mat4_t view, proj;
    editor_camera_view_matrix(&fvp->camera, &view);
    editor_camera_projection_matrix(&fvp->camera, aspect, &proj);
    mat4_t vp = mat4_mul(proj, view);

    uint32_t cap = ed->entities.capacity;
    for (uint32_t i = 0; i < cap; i++) {
        const edit_entity_t *ent = edit_entity_store_get(&ed->entities, i);
        if (!ent || ent->pending_delete || ent->hidden) continue;

        /* Project geometry center to clip space. */
        float gc[3];
        edit_entity_geometry_center(ent, gc);
        float px = vp.m[0] * gc[0] + vp.m[4] * gc[1]
                  + vp.m[8]  * gc[2] + vp.m[12];
        float py = vp.m[1] * gc[0] + vp.m[5] * gc[1]
                  + vp.m[9]  * gc[2] + vp.m[13];
        float pw = vp.m[3] * gc[0] + vp.m[7] * gc[1]
                  + vp.m[11] * gc[2] + vp.m[15];

        /* Skip entities behind the camera. */
        if (pw <= 0.0f) continue;

        /* NDC to normalized viewport coords [0,1]. */
        float nx = (px / pw) * 0.5f + 0.5f;
        float ny = 0.5f - (py / pw) * 0.5f; /* Flip Y: NDC up → screen down. */

        if (nx >= bx0 && nx <= bx1 && ny >= by0 && ny <= by1) {
            edit_selection_add(&ed->selection, i);
            /* Make the last selected entity the active object. */
            ed->active_object_id = i;
        }
    }

    /* ---- Bone box select (prefab or regular mode with bone selection) ---- */
    if (ed->bone_selection.entity_id != EDIT_BONE_SEL_NONE) {
        uint32_t root_id = ed->bone_selection.entity_id;
        const edit_entity_t *root_ent =
            edit_entity_store_get(&ed->entities, root_id);
        if (!root_ent) return;

        /* Look up skeleton. */
        uint8_t stype = 0, ssize = 0;
        const void *sp = entity_attrs_get(&root_ent->attrs,
            SCRIPT_KEY_SKEL_PATH, &stype, &ssize);
        if (!sp || stype != SCRIPT_ATTR_STR || ssize == 0) return;

        const char *path = (const char *)sp;
        const char *slash = strrchr(path, '/');
        const char *fname = slash ? slash + 1 : path;

        const edit_skeleton_entry_t *sentry =
            edit_skeleton_registry_get(&ed->skeleton_registry, fname);
        if (!sentry) return;

        const skeleton_def_t *skel = &sentry->skel;
        /* Use per-entity override rest_world if available. */
        const mat4_t *rest_world = skel->rest_world;
        const bone_pose_block_t *bp =
            bone_pose_store_get(&ed->bone_poses, root_id);
        if (bp) rest_world = bp->rest_world;

        mat4_t model = edit_entity_build_model_matrix(root_ent);

        for (uint32_t bi = 0; bi < skel->joint_count; bi++) {
            /* Bone head position in world space. */
            float lx = rest_world[bi].m[12];
            float ly = rest_world[bi].m[13];
            float lz = rest_world[bi].m[14];

            float wx = model.m[0]*lx + model.m[4]*ly + model.m[8]*lz  + model.m[12];
            float wy = model.m[1]*lx + model.m[5]*ly + model.m[9]*lz  + model.m[13];
            float wz = model.m[2]*lx + model.m[6]*ly + model.m[10]*lz + model.m[14];

            /* Project to clip space. */
            float px = vp.m[0]*wx + vp.m[4]*wy + vp.m[8]*wz  + vp.m[12];
            float py = vp.m[1]*wx + vp.m[5]*wy + vp.m[9]*wz  + vp.m[13];
            float pw = vp.m[3]*wx + vp.m[7]*wy + vp.m[11]*wz + vp.m[15];

            if (pw <= 0.0f) continue;

            float bnx = (px / pw) * 0.5f + 0.5f;
            float bny = 0.5f - (py / pw) * 0.5f;

            if (bnx >= bx0 && bnx <= bx1 && bny >= by0 && bny <= by1) {
                edit_bone_selection_add(&ed->bone_selection,
                                        root_id, bi);
            }
        }
    }
}

/**
 * @brief Handle mouse button up: stop divider drag, update Clay state.
 */
static bool handle_mouse_up(scene_editor_t *ed, const SDL_MouseButtonEvent *ev) {
    if (ev->button == SDL_BUTTON_LEFT) {
        ed->ui.mouse_down = false;
        viewport_state_t *fvp = scene_focused_vp(ed);

        /* End box select: project entities to screen, select those inside. */
        if (fvp->box_selecting) {
            fvp->box_selecting = false;
            finish_box_select_(ed);
            return true;
        }

        /* End gizmo drag (including free-move): send server commands. */
        if (fvp->gizmo.dragging) {
            /* Snap euler axes BEFORE sending so the server gets the
             * snapped absolute orientation (not the unsnapped delta). */
            snap_selected_euler_axes(ed);
            send_gizmo_commands(ed, fvp->gizmo_drag_accum);
            fvp->gizmo.dragging = false;
            fvp->gizmo.active_axis = GIZMO_AXIS_NONE;
            fvp->free_dragging = false;
            fvp->bone_drag_index = UINT32_MAX;
            return true;
        }

        /* End BSP divider drag. */
        if (ed->dragging_bsp_node >= 0) {
            ed->dragging_bsp_node = -1;
            return true;
        }

        if (ed->dragging_divider != DIVIDER_NONE) {
            ed->dragging_divider = DIVIDER_NONE;
            return true;
        }
        if (ed->ui.scrollbar_dragging != 0) {
            ed->ui.scrollbar_dragging = 0;
            return true;
        }
    } else if (ev->button == SDL_BUTTON_MIDDLE) {
        ed->ui.middle_mouse_down = false;
    } else if (ev->button == SDL_BUTTON_RIGHT) {
        ed->ui.right_mouse_down = false;
    }
    return false;
}

/**
 * @brief Handle mouse motion: drag divider if active, update position.
 */
static bool handle_mouse_motion(scene_editor_t *ed,
                                 const SDL_MouseMotionEvent *ev) {
    /* Always update mouse position for Clay. */
    ed->ui.mouse_x = (float)ev->x;
    ed->ui.mouse_y = (float)ev->y;

    viewport_state_t *fvp = scene_focused_vp(ed);

    /* BSP divider drag: adjust split ratio of the dragged node. */
    if (ed->dragging_bsp_node >= 0) {
        viewport_bsp_node_t *node =
            &ed->vp_bsp.nodes[ed->dragging_bsp_node];
        /* Determine total size along the split axis. */
        panel_rect_t vp_panel = panel_layout_get_rect(&ed->layout,
                                                       PANEL_VIEWPORT);
        int total_size = (node->split == SPLIT_VERTICAL)
                         ? vp_panel.w : vp_panel.h;
        /* Scale physical pixel delta to logical pixels. */
        float sc = ed->clay_be.ui_scale;
        if (sc < 1.0f) sc = 1.0f;
        int raw_delta = (node->split == SPLIT_HORIZONTAL)
                        ? ev->yrel : ev->xrel;
        int delta = (int)((float)raw_delta / sc);
        viewport_bsp_drag_divider(&ed->vp_bsp,
                                   (uint8_t)ed->dragging_bsp_node,
                                   delta, total_size);
        return true;
    }

    /* Gizmo drag: convert pixel delta to constrained world delta. */
    if (fvp->gizmo.dragging) {
        vec3_t delta = {0, 0, 0};

        /* Free-move: translate on the camera-facing plane. */
        if (fvp->free_dragging) {
            /* Check for Ctrl surface snap. */
            SDL_Keymod free_mod = SDL_GetModState();
            if (free_mod & KMOD_CTRL) {
                try_surface_snap_(ed, (free_mod & KMOD_SHIFT) != 0);
                return true;
            }

            mat4_t view;
            editor_camera_view_matrix(&fvp->camera, &view);

            /* Camera right = row 0 of view, camera up = row 1 of view.
             * View matrix rows are: right, up, -forward (column-major). */
            vec3_t cam_right = {view.m[0], view.m[4], view.m[8]};
            vec3_t cam_up    = {view.m[1], view.m[5], view.m[9]};

            vec3_t eye = editor_camera_eye_position(&fvp->camera);
            float cam_dist = viewport_gizmo_screen_scale(
                &fvp->gizmo.position, &eye,
                fvp->camera.fov);
            float speed = cam_dist * GIZMO_DRAG_SPEED;

            /* Mouse X → camera right, mouse Y → camera up.
             * FBO is displayed Y-flipped by Clay, so SDL's downward-positive
             * Y already matches the visual direction — no negation. */
            float mx = (float)ev->xrel * speed;
            float my = (float)ev->yrel * speed;
            delta.x = cam_right.x * mx + cam_up.x * my;
            delta.y = cam_right.y * mx + cam_up.y * my;
            delta.z = cam_right.z * mx + cam_up.z * my;

            /* Accumulate raw delta, then compute snapped increment. */
            fvp->gizmo_drag_accum.x += delta.x;
            fvp->gizmo_drag_accum.y += delta.y;
            fvp->gizmo_drag_accum.z += delta.z;
            vec3_t snapped = snap_apply_position(&ed->snap,
                fvp->snap_origin_pos, fvp->gizmo_drag_accum);
            vec3_t inc = {
                snapped.x - fvp->snap_applied_delta.x,
                snapped.y - fvp->snap_applied_delta.y,
                snapped.z - fvp->snap_applied_delta.z,
            };
            fvp->snap_applied_delta = snapped;
            apply_gizmo_drag(ed, inc);
            return true;
        }

        /* Get the oriented axis direction from the gizmo orientation.
         * Column 0=X, 1=Y, 2=Z of the orientation matrix. */
        int axis_col = -1;
        switch (fvp->gizmo.active_axis) {
        case GIZMO_AXIS_X: axis_col = 0; break;
        case GIZMO_AXIS_Y: axis_col = 1; break;
        case GIZMO_AXIS_Z: axis_col = 2; break;
        default: break;
        }

        if (fvp->gizmo.mode == GIZMO_MODE_ROTATE) {
            /* Project the rotation axis onto screen space to determine
             * which mouse direction maps to positive rotation.
             * This keeps rotation intuitive regardless of camera angle. */
            mat4_t view;
            editor_camera_view_matrix(&fvp->camera, &view);

            /* Get oriented axis direction from gizmo basis. */
            vec3_t axis_dir = {0, 0, 0};
            if (axis_col >= 0) {
                const mat4_t *o = &fvp->gizmo.orientation;
                axis_dir.x = o->m[axis_col * 4 + 0];
                axis_dir.y = o->m[axis_col * 4 + 1];
                axis_dir.z = o->m[axis_col * 4 + 2];
            }

            /* Transform axis to view space (rotation only). */
            float sx = view.m[0] * axis_dir.x + view.m[4] * axis_dir.y
                      + view.m[8] * axis_dir.z;
            float sy = view.m[1] * axis_dir.x + view.m[5] * axis_dir.y
                      + view.m[9] * axis_dir.z;

            /* Screen-space perpendicular: mouse along (sy, -sx)
             * produces positive rotation around the axis. */
            float perp_x =  sy;
            float perp_y = -sx;
            float perp_len = sqrtf(perp_x * perp_x + perp_y * perp_y);

            float rot_amount;
            if (perp_len > 0.05f) {
                perp_x /= perp_len;
                perp_y /= perp_len;
                float mouse_proj = (float)ev->xrel * perp_x
                                  + (float)ev->yrel * perp_y;
                rot_amount = mouse_proj * GIZMO_ROTATE_SPEED;
            } else {
                rot_amount = (float)ev->xrel * GIZMO_ROTATE_SPEED;
            }

            /* Accumulate raw rotation, snap absolute euler, apply increment. */
            fvp->snap_rot_accum_deg += rot_amount;
            int snap_axis = (axis_col >= 0) ? axis_col : 0;

            /* Compute absolute target euler = start + accum, snap it,
             * then derive the corrected delta from start. */
            float abs_target = fvp->snap_rot_origin[snap_axis]
                             + fvp->snap_rot_accum_deg;
            float snapped_abs = snap_apply_rotation(&ed->snap,
                abs_target, snap_axis);
            /* Corrected delta from the original orientation. */
            float snapped_delta = snapped_abs
                                - fvp->snap_rot_origin[snap_axis];
            float inc_deg = snapped_delta - fvp->snap_rot_applied_deg;
            fvp->snap_rot_applied_deg = snapped_delta;

            if (fabsf(inc_deg) > 1e-6f) {
                quat_t dq = quat_from_axis_angle(
                    axis_dir, inc_deg * INPUT_DEG_TO_RAD, 1e-8f);
                apply_gizmo_rotate(ed, dq);
                fvp->gizmo_rot_accum = quat_normalize_safe(
                    quat_mul(dq, fvp->gizmo_rot_accum), 1e-8f);
            }
            return true;
        } else if (gizmo_axis_is_planar(fvp->gizmo.active_axis)) {
            /* Planar constraint: use the plane's own orthonormal
             * tangent/bitangent from the gizmo orientation.  Project
             * both to screen space and invert the resulting 2×2 matrix
             * so screen-space mouse delta maps correctly to plane
             * coordinates regardless of viewing angle. */
            mat4_t view;
            editor_camera_view_matrix(&fvp->camera, &view);
            const mat4_t *o = &fvp->gizmo.orientation;

            /* Determine which two orientation columns are the plane axes. */
            int col_a, col_b;
            switch (fvp->gizmo.active_axis) {
            case GIZMO_AXIS_XY: col_a = 0; col_b = 1; break;
            case GIZMO_AXIS_XZ: col_a = 0; col_b = 2; break;
            case GIZMO_AXIS_YZ: col_a = 1; col_b = 2; break;
            default:            col_a = 0; col_b = 1; break;
            }

            /* Plane tangent and bitangent (already orthonormal). */
            vec3_t tang = {o->m[col_a*4+0], o->m[col_a*4+1], o->m[col_a*4+2]};
            vec3_t btan = {o->m[col_b*4+0], o->m[col_b*4+1], o->m[col_b*4+2]};

            /* Project both to screen space (view rotation, XY only). */
            float sa_x = view.m[0]*tang.x + view.m[4]*tang.y + view.m[8]*tang.z;
            float sa_y = view.m[1]*tang.x + view.m[5]*tang.y + view.m[9]*tang.z;
            float sb_x = view.m[0]*btan.x + view.m[4]*btan.y + view.m[8]*btan.z;
            float sb_y = view.m[1]*btan.x + view.m[5]*btan.y + view.m[9]*btan.z;

            /* Invert the 2×2 screen-projection matrix [sa | sb]:
             *   [amount_a]   1   [ sb_y  -sb_x ] [mouse_dx]
             *   [amount_b] = - * [-sa_y   sa_x ] [mouse_dy]
             *                det                             */
            float det = sa_x * sb_y - sa_y * sb_x;

            vec3_t eye = editor_camera_eye_position(&fvp->camera);
            float cam_dist = viewport_gizmo_screen_scale(
                &fvp->gizmo.position, &eye,
                fvp->camera.fov);
            float speed = cam_dist * GIZMO_DRAG_SPEED;
            float mx = (float)ev->xrel;
            float my = (float)ev->yrel;

            /* Compute per-axis amounts (how much to move along each
             * plane axis).  Used by both translate and scale. */
            float amount_a = 0.0f, amount_b = 0.0f;

            if (fabsf(det) > 0.01f) {
                float inv_det = 1.0f / det;
                amount_a = ( sb_y * mx - sb_x * my) * inv_det;
                amount_b = (-sa_y * mx + sa_x * my) * inv_det;
            } else {
                /* Plane is edge-on: one axis is screen-visible, the
                 * other points into/out of the screen.  Map mouse
                 * motion along the visible axis's screen direction to
                 * that axis, and perpendicular motion to the hidden. */
                float la = sqrtf(sa_x*sa_x + sa_y*sa_y);
                float lb = sqrtf(sb_x*sb_x + sb_y*sb_y);
                bool a_vis = (la >= lb);
                float vsx = a_vis ? sa_x : sb_x;
                float vsy = a_vis ? sa_y : sb_y;
                float vlen = a_vis ? la : lb;
                vec3_t hid_dir = a_vis ? btan : tang;

                float vis_amt = 0.0f, hid_amt = 0.0f;
                if (vlen > 0.01f) {
                    float nvx = vsx / vlen, nvy = vsy / vlen;
                    vis_amt = mx * nvx + my * nvy;

                    float px = -nvy, py = nvx;
                    float perp_proj = mx * px + my * py;
                    float hz = view.m[2]*hid_dir.x + view.m[6]*hid_dir.y
                              + view.m[10]*hid_dir.z;
                    float sign = (hz < 0.0f) ? 1.0f : -1.0f;
                    hid_amt = perp_proj * sign;
                }
                amount_a = a_vis ? vis_amt : hid_amt;
                amount_b = a_vis ? hid_amt : vis_amt;
            }

            if (fvp->gizmo.mode == GIZMO_MODE_SCALE) {
                /* Scale: apply per-axis scale factors along each plane
                 * axis independently. The normal axis gets zero delta
                 * so scaling is truly planar-constrained. */
                float sa = amount_a * speed;
                float sb = amount_b * speed;
                /* Map plane axis indices back to XYZ scale components. */
                float scale_d[3] = {0.0f, 0.0f, 0.0f};
                scale_d[col_a] = sa;
                scale_d[col_b] = sb;
                delta.x = scale_d[0];
                delta.y = scale_d[1];
                delta.z = scale_d[2];
            } else {
                /* Translate: reconstruct world-space displacement. */
                delta.x = (tang.x * amount_a + btan.x * amount_b) * speed;
                delta.y = (tang.y * amount_a + btan.y * amount_b) * speed;
                delta.z = (tang.z * amount_a + btan.z * amount_b) * speed;
            }
        } else {
            /* Single-axis translate/scale: project mouse motion along
             * the oriented axis direction in screen space. */
            mat4_t view;
            editor_camera_view_matrix(&fvp->camera, &view);

            vec3_t axis_dir = {0, 0, 0};
            if (axis_col >= 0) {
                const mat4_t *o = &fvp->gizmo.orientation;
                axis_dir.x = o->m[axis_col * 4 + 0];
                axis_dir.y = o->m[axis_col * 4 + 1];
                axis_dir.z = o->m[axis_col * 4 + 2];
            }

            /* Project world axis to screen space (view rotation only). */
            float sx = view.m[0] * axis_dir.x + view.m[4] * axis_dir.y
                      + view.m[8] * axis_dir.z;
            float sy = view.m[1] * axis_dir.x + view.m[5] * axis_dir.y
                      + view.m[9] * axis_dir.z;
            float slen = sqrtf(sx * sx + sy * sy);

            float mouse_proj;
            if (slen > 0.05f) {
                /* Normal case: axis has visible screen projection.
                 * Dot mouse delta with screen-space axis direction. */
                sx /= slen;
                sy /= slen;
                mouse_proj = (float)ev->xrel * sx
                            + (float)ev->yrel * sy;
            } else {
                /* Edge case: axis points toward/away from camera
                 * (nearly parallel to view direction). Use mouse Y
                 * to move along the axis, with sign from view-space Z
                 * so dragging up moves the object toward the camera. */
                float sz = view.m[2] * axis_dir.x + view.m[6] * axis_dir.y
                          + view.m[10] * axis_dir.z;
                float sign = (sz < 0.0f) ? 1.0f : -1.0f;
                mouse_proj = (float)ev->yrel * sign;
            }

            vec3_t eye = editor_camera_eye_position(&fvp->camera);
            float cam_dist = viewport_gizmo_screen_scale(
                &fvp->gizmo.position, &eye,
                fvp->camera.fov);
            float speed = cam_dist * GIZMO_DRAG_SPEED;

            /* Apply along the world-space axis direction. */
            delta.x = axis_dir.x * mouse_proj * speed;
            delta.y = axis_dir.y * mouse_proj * speed;
            delta.z = axis_dir.z * mouse_proj * speed;
        }

        /* Accumulate raw delta. */
        fvp->gizmo_drag_accum.x += delta.x;
        fvp->gizmo_drag_accum.y += delta.y;
        fvp->gizmo_drag_accum.z += delta.z;

        if (fvp->gizmo.mode == GIZMO_MODE_SCALE) {
            /* Track multiplicative scale factor. */
            fvp->gizmo_scale_accum.x *= (1.0f + delta.x);
            fvp->gizmo_scale_accum.y *= (1.0f + delta.y);
            fvp->gizmo_scale_accum.z *= (1.0f + delta.z);

            /* Snap the absolute scale and apply incremental difference. */
            vec3_t snapped = snap_apply_scale(&ed->snap,
                fvp->snap_origin_scale, fvp->gizmo_scale_accum);
            /* Incremental scale delta from last applied. */
            vec3_t inc;
            inc.x = (1.0f + snapped.x - fvp->snap_applied_scale.x)
                    / 1.0f - 1.0f;
            /* Actually: entities are at scale (orig * applied_factor).
             * We need to apply (snapped / applied) multiplicatively.
             * Equivalently, delta = snapped_factor / applied_factor - 1. */
            float safe_div_x = (fabsf(fvp->snap_applied_scale.x) > 1e-9f)
                ? fvp->snap_applied_scale.x : 1.0f;
            float safe_div_y = (fabsf(fvp->snap_applied_scale.y) > 1e-9f)
                ? fvp->snap_applied_scale.y : 1.0f;
            float safe_div_z = (fabsf(fvp->snap_applied_scale.z) > 1e-9f)
                ? fvp->snap_applied_scale.z : 1.0f;
            inc.x = snapped.x / safe_div_x - 1.0f;
            inc.y = snapped.y / safe_div_y - 1.0f;
            inc.z = snapped.z / safe_div_z - 1.0f;
            fvp->snap_applied_scale = snapped;
            apply_gizmo_drag(ed, inc);
        } else {
            /* Translate mode. Check if Ctrl is held for surface snap. */
            SDL_Keymod kmod = SDL_GetModState();
            if (kmod & KMOD_CTRL) {
                try_surface_snap_(ed, (kmod & KMOD_SHIFT) != 0);
            } else {
                /* Normal translate: snap absolute position, apply incremental. */
                vec3_t snapped = snap_apply_position(&ed->snap,
                    fvp->snap_origin_pos, fvp->gizmo_drag_accum);
                vec3_t inc = {
                    snapped.x - fvp->snap_applied_delta.x,
                    snapped.y - fvp->snap_applied_delta.y,
                    snapped.z - fvp->snap_applied_delta.z,
                };
                fvp->snap_applied_delta = snapped;
                apply_gizmo_drag(ed, inc);
            }
        }
        return true;
    }

    /* Right mouse or middle mouse drag: camera control per nav mode. */
    if (ed->ui.right_mouse_down || ed->ui.middle_mouse_down) {
        SDL_Keymod mod = SDL_GetModState();
        nav_mode_t nm = fvp->nav_mode;

        if (nm == NAV_MODE_FLY) {
            /* Fly mode: mouselook (rotate yaw/pitch). */
            editor_camera_orbit(&fvp->camera,
                                 -(float)ev->xrel * CAMERA_ORBIT_SPEED,
                                 -(float)ev->yrel * CAMERA_ORBIT_SPEED);
        } else if (nm == NAV_MODE_PAN_ZOOM) {
            /* Pan-zoom: always pan, never orbit. */
            editor_camera_pan(&fvp->camera,
                               -(float)ev->xrel * CAMERA_PAN_SPEED,
                               (float)ev->yrel * CAMERA_PAN_SPEED);
        } else {
            /* Orbit modes: shift=pan, otherwise orbit. */
            if (mod & KMOD_SHIFT) {
                editor_camera_pan(&fvp->camera,
                                   -(float)ev->xrel * CAMERA_PAN_SPEED,
                                   (float)ev->yrel * CAMERA_PAN_SPEED);
            } else {
                /* For orbit-cursor, snap focus to cursor before orbiting. */
                if (nm == NAV_MODE_ORBIT_CURSOR) {
                    fvp->camera.focus = fvp->cursor_3d;
                }
                editor_camera_orbit(&fvp->camera,
                                     -(float)ev->xrel * CAMERA_ORBIT_SPEED,
                                     -(float)ev->yrel * CAMERA_ORBIT_SPEED);
            }
        }
        return true;
    }

    /* Scrollbar drag: map mouse Y delta to scroll position. */
    if (ed->ui.scrollbar_dragging != 0) {
        float sc = ed->clay_be.ui_scale;
        if (sc < 1.0f) sc = 1.0f;
        float ly = (float)ev->y / sc;
        float dy = ly - ed->ui.scrollbar_drag_y;

        int sb = ed->ui.scrollbar_dragging;
        int total = 0, visible = 0;
        if (sb == 1) {
            total = ed->ui.outliner_total;
            visible = ed->ui.outliner_visible_lines;
        } else if (sb == 2) {
            total = ed->ui.inspector_total;
            visible = ed->ui.inspector_visible_lines;
        } else if (sb == 3) {
            total = ed->ui.tui_log_count;
            visible = ed->ui.tui_log_visible;
        } else if (sb == 4) {
            total = ed->ui.asset_browser_total;
            visible = ed->ui.asset_browser_visible_lines;
        } else if (sb == 5) {
            total = ed->ui.prefab_outliner_total;
            visible = ed->ui.prefab_outliner_visible_lines;
        }

        int max_scroll = total - visible;
        if (max_scroll < 0) max_scroll = 0;

        /* Get track height from panel rect. */
        float track_h;
        if (sb == 4) {
            /* Asset browser: bottom 40% of outliner panel. */
            panel_rect_t r = panel_layout_get_rect(&ed->layout, PANEL_OUTLINER);
            int browser_h = r.h - (r.h * 3) / 5;
            track_h = (float)(browser_h - THEME_ROW_HEIGHT);
        } else if (sb == 5) {
            /* Prefab outliner: top 60% of outliner panel. */
            panel_rect_t r = panel_layout_get_rect(&ed->layout, PANEL_OUTLINER);
            int outliner_h = (r.h * 3) / 5;
            track_h = (float)(outliner_h - THEME_ROW_HEIGHT);
        } else {
            panel_id_t sb_panels[] = {PANEL_OUTLINER, PANEL_INSPECTOR, PANEL_TUI};
            panel_rect_t r = panel_layout_get_rect(&ed->layout, sb_panels[sb - 1]);
            track_h = (float)(r.h - THEME_ROW_HEIGHT);
        }
        if (track_h < 1.0f) track_h = 1.0f;

        /* Convert pixel drag to scroll units. */
        float scroll_per_px = (float)max_scroll / track_h;
        /* TUI scroll is inverted: scroll=0 is bottom (newest),
         * so dragging down should decrease scroll. */
        int delta_scroll = (int)(dy * scroll_per_px);
        if (sb == 3) delta_scroll = -delta_scroll;
        int new_scroll = ed->ui.scrollbar_drag_scroll + delta_scroll;
        if (new_scroll < 0) new_scroll = 0;
        if (new_scroll > max_scroll) new_scroll = max_scroll;

        if (sb == 1) ed->ui.outliner_scroll = new_scroll;
        else if (sb == 2) ed->ui.inspector_scroll = new_scroll;
        else if (sb == 3) ed->ui.tui_log_scroll = new_scroll;
        else if (sb == 4) ed->ui.asset_browser_scroll = new_scroll;
        else if (sb == 5) ed->ui.prefab_outliner_scroll = new_scroll;

        return true;
    }

    if (ed->dragging_divider == DIVIDER_NONE) return false;

    /* Scale physical pixel delta to logical pixels for the layout. */
    float sc = ed->clay_be.ui_scale;
    if (sc < 1.0f) sc = 1.0f;
    int raw_delta = (ed->dragging_divider == DIVIDER_BOTTOM) ? ev->yrel : ev->xrel;
    int delta = (int)((float)raw_delta / sc);
    panel_layout_drag_divider(&ed->layout, ed->dragging_divider, delta);
    return true;
}

/**
 * @brief Push a command into the history ring buffer.
 *
 * Skips empty strings and duplicates of the most recent entry.
 */
static void tui_history_push(scene_ui_state_t *ui, const char *cmd) {
    if (!cmd || cmd[0] == '\0') return;

    /* Skip if identical to previous entry. */
    if (ui->tui_history_count > 0) {
        int prev = (ui->tui_history_head - 1 + UI_TUI_HISTORY_MAX)
                   % UI_TUI_HISTORY_MAX;
        if (strcmp(ui->tui_history[prev], cmd) == 0) return;
    }

    strncpy(ui->tui_history[ui->tui_history_head], cmd,
            UI_TUI_INPUT_MAX - 1);
    ui->tui_history[ui->tui_history_head][UI_TUI_INPUT_MAX - 1] = '\0';
    ui->tui_history_head = (ui->tui_history_head + 1) % UI_TUI_HISTORY_MAX;
    if (ui->tui_history_count < UI_TUI_HISTORY_MAX) {
        ui->tui_history_count++;
    }
}

/**
 * @brief Handle tab completion for TUI command input.
 *
 * Completes command names using ctrl_cmd_complete(). If a single match
 * is found, replaces the input. If multiple matches, logs them and
 * fills the common prefix.
 */
static void handle_tui_tab(scene_editor_t *ed) {
    scene_ui_state_t *ui = &ed->ui;
    if (ui->tui_input_len == 0) return;

    /* Only complete the first word (command name). */
    char *space = strchr(ui->tui_input, ' ');
    if (space) return;  /* Already past command name — no arg completion yet. */

    const char *matches[32];
    uint32_t match_count = ctrl_cmd_complete(ui->tui_input, matches, 32);

    if (match_count == 1) {
        /* Single match — replace input with match + space. */
        size_t len = strlen(matches[0]);
        if (len < UI_TUI_INPUT_MAX - 2) {
            memcpy(ui->tui_input, matches[0], len);
            ui->tui_input[len] = ' ';
            ui->tui_input_len = (int)(len + 1);
            ui->tui_input[ui->tui_input_len] = '\0';
            ui->tui_cursor = ui->tui_input_len;
        }
    } else if (match_count > 1) {
        /* Multiple matches — log them. */
        char line[512];
        int pos = 0;
        for (uint32_t i = 0; i < match_count; i++) {
            int n = snprintf(line + pos, sizeof(line) - (size_t)pos,
                             "  %s", matches[i]);
            if (n > 0) pos += n;
        }
        scene_ui_tui_log(ui, line);

        /* Fill common prefix. */
        size_t common = strlen(matches[0]);
        for (uint32_t i = 1; i < match_count; i++) {
            size_t j = 0;
            while (j < common && matches[0][j] == matches[i][j]) j++;
            common = j;
        }
        if (common > (size_t)ui->tui_input_len) {
            memcpy(ui->tui_input, matches[0], common);
            ui->tui_input_len = (int)common;
            ui->tui_input[ui->tui_input_len] = '\0';
            ui->tui_cursor = ui->tui_input_len;
        }
    }
}

/**
 * @brief Insert a character at the TUI cursor position.
 */
static void tui_insert_char(scene_ui_state_t *ui, char ch) {
    if (ui->tui_input_len >= UI_TUI_INPUT_MAX - 1) return;
    /* Shift characters right to make room at cursor. */
    memmove(&ui->tui_input[ui->tui_cursor + 1],
            &ui->tui_input[ui->tui_cursor],
            (size_t)(ui->tui_input_len - ui->tui_cursor));
    ui->tui_input[ui->tui_cursor] = ch;
    ui->tui_cursor++;
    ui->tui_input_len++;
    ui->tui_input[ui->tui_input_len] = '\0';
}

/**
 * @brief Handle key down when TUI has keyboard focus.
 *
 * Handles cursor movement, backspace, delete, enter (submit),
 * and escape (deactivate). Returns true if the key was consumed.
 */
static bool handle_tui_key(scene_editor_t *ed, const SDL_KeyboardEvent *ev) {
    scene_ui_state_t *ui = &ed->ui;
    SDL_Keycode key = ev->keysym.sym;

    switch (key) {
    case SDLK_ESCAPE:
        /* Deactivate TUI input, return focus to viewport. */
        ui->tui_active = false;
        panel_layout_focus_viewport(&ed->layout);
        return true;

    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        /* Submit the command if non-empty. */
        if (ui->tui_input_len > 0) {
            /* Push to command history before clearing. */
            tui_history_push(ui, ui->tui_input);
            ui->tui_history_index = -1;

            /* Echo the typed command to the log. */
            scene_ui_tui_log(ui, ui->tui_input);

            /* Copy command to tui_cmd before clearing input. */
            memcpy(ui->tui_cmd, ui->tui_input, (size_t)(ui->tui_input_len + 1));
            ui->action = UI_ACTION_TUI_COMMAND;

            /* Clear input and deactivate TUI. */
            ui->tui_input[0] = '\0';
            ui->tui_input_len = 0;
            ui->tui_cursor = 0;
            ui->tui_active = false;
        }
        return true;

    case SDLK_BACKSPACE:
        if (ui->tui_cursor > 0) {
            memmove(&ui->tui_input[ui->tui_cursor - 1],
                    &ui->tui_input[ui->tui_cursor],
                    (size_t)(ui->tui_input_len - ui->tui_cursor));
            ui->tui_cursor--;
            ui->tui_input_len--;
            ui->tui_input[ui->tui_input_len] = '\0';
        }
        return true;

    case SDLK_DELETE:
        if (ui->tui_cursor < ui->tui_input_len) {
            memmove(&ui->tui_input[ui->tui_cursor],
                    &ui->tui_input[ui->tui_cursor + 1],
                    (size_t)(ui->tui_input_len - ui->tui_cursor - 1));
            ui->tui_input_len--;
            ui->tui_input[ui->tui_input_len] = '\0';
        }
        return true;

    case SDLK_LEFT:
        if (ui->tui_cursor > 0) ui->tui_cursor--;
        return true;

    case SDLK_RIGHT:
        if (ui->tui_cursor < ui->tui_input_len) ui->tui_cursor++;
        return true;

    case SDLK_HOME:
        ui->tui_cursor = 0;
        return true;

    case SDLK_END:
        ui->tui_cursor = ui->tui_input_len;
        return true;

    case SDLK_UP:
        /* Browse command history backward. */
        if (ui->tui_history_count > 0) {
            if (ui->tui_history_index == -1) {
                /* Stash current input before browsing. */
                memcpy(ui->tui_history_stash, ui->tui_input,
                       (size_t)(ui->tui_input_len + 1));
                ui->tui_history_index = 0;
            } else if (ui->tui_history_index < ui->tui_history_count - 1) {
                ui->tui_history_index++;
            }
            /* Copy history entry to input. */
            int slot = (ui->tui_history_head - 1 - ui->tui_history_index
                        + UI_TUI_HISTORY_MAX) % UI_TUI_HISTORY_MAX;
            strncpy(ui->tui_input, ui->tui_history[slot],
                    UI_TUI_INPUT_MAX - 1);
            ui->tui_input[UI_TUI_INPUT_MAX - 1] = '\0';
            ui->tui_input_len = (int)strlen(ui->tui_input);
            ui->tui_cursor = ui->tui_input_len;
        }
        return true;

    case SDLK_DOWN:
        /* Browse command history forward. */
        if (ui->tui_history_index > 0) {
            ui->tui_history_index--;
            int slot = (ui->tui_history_head - 1 - ui->tui_history_index
                        + UI_TUI_HISTORY_MAX) % UI_TUI_HISTORY_MAX;
            strncpy(ui->tui_input, ui->tui_history[slot],
                    UI_TUI_INPUT_MAX - 1);
            ui->tui_input[UI_TUI_INPUT_MAX - 1] = '\0';
            ui->tui_input_len = (int)strlen(ui->tui_input);
            ui->tui_cursor = ui->tui_input_len;
        } else if (ui->tui_history_index == 0) {
            /* Restore stashed input. */
            ui->tui_history_index = -1;
            memcpy(ui->tui_input, ui->tui_history_stash,
                   UI_TUI_INPUT_MAX);
            ui->tui_input_len = (int)strlen(ui->tui_input);
            ui->tui_cursor = ui->tui_input_len;
        }
        return true;

    case SDLK_TAB:
        handle_tui_tab(ed);
        return true;

    case SDLK_u:
        /* Ctrl+U: clear line. */
        if (ev->keysym.mod & KMOD_CTRL) {
            ui->tui_input[0] = '\0';
            ui->tui_input_len = 0;
            ui->tui_cursor = 0;
            return true;
        }
        break;

    default:
        break;
    }

    return false;
}

/**
 * @brief Handle SDL_TEXTINPUT event: insert typed text into TUI input.
 */
static bool handle_text_input(scene_editor_t *ed, const char *text) {
    if (!ed->ui.tui_active) return false;

    /* Skip the ':' character that triggered TUI activation. */
    if (ed->ui.tui_skip_next_text) {
        ed->ui.tui_skip_next_text = false;
        return true;
    }

    for (int i = 0; text[i] != '\0'; i++) {
        char ch = text[i];
        if (ch >= 32 && ch < 127) {
            tui_insert_char(&ed->ui, ch);
        }
    }
    return true;
}

/**
 * @brief Handle key down: panel toggles, entity operations, TUI routing.
 */
/** @brief Returns true for keys that should fire continuously while held
 *  (camera movement, zoom, numpad orbit, scroll). */
static bool key_is_continuous(SDL_Keycode key) {
    switch (key) {
    /* Fly mode camera movement. */
    case SDLK_UP: case SDLK_DOWN: case SDLK_LEFT: case SDLK_RIGHT:
    /* Zoom. */
    case SDLK_PLUS: case SDLK_EQUALS: case SDLK_MINUS:
    /* Numpad orbit. */
    case SDLK_2: case SDLK_4: case SDLK_6: case SDLK_8: case SDLK_9:
    /* Scroll (handled earlier but included for completeness). */
    case SDLK_PAGEUP: case SDLK_PAGEDOWN: case SDLK_HOME: case SDLK_END:
        return true;
    default:
        return false;
    }
}

static bool handle_key_down(scene_editor_t *ed, const SDL_KeyboardEvent *ev) {
    SDL_Keycode key = ev->keysym.sym;
    viewport_state_t *fvp = scene_focused_vp(ed);

    /* If TUI is active, route keys to TUI input handler first. */
    if (ed->ui.tui_active) {
        return handle_tui_key(ed, ev);
    }

    /* One-shot guard: for non-continuous keys, require a KEYUP between
     * presses. This prevents double-fire from OS-level key repeat or
     * brief release/repress generating two KEYDOWN events. */
    if (!key_is_continuous(key)) {
        if (!key_mark_held(&ed->ui, (uint32_t)key)) {
            return true;  /* Already held — swallow the event. */
        }
    }

    /* Alt+Arrow: split focused viewport in the BSP tree.
     * Alt+Number: focus viewport by display number (1-9). */
    if (ev->keysym.mod & KMOD_ALT) {
        /* Alt+1..9: switch focus to viewport by display number. */
        if (key >= SDLK_1 && key <= SDLK_9) {
            int target_num = key - SDLK_1 + 1;
            int count = 0;
            for (int i = 0; i < VIEWPORT_MAX_COUNT; i++) {
                if (!ed->viewports[i].active) continue;
                if (ed->viewports[i].rect.w <= 0) continue;
                count++;
                if (count == target_num) {
                    if ((uint8_t)i != ed->vp_bsp.focused_viewport) {
                        ed->prev_focused_vp = ed->vp_bsp.focused_viewport;
                    }
                    ed->vp_bsp.focused_viewport = (uint8_t)i;
                    break;
                }
            }
            return true;
        }

        viewport_split_dir_t split_dir = SPLIT_NONE;
        bool original_first = true;
        switch (key) {
        case SDLK_LEFT:
            split_dir = SPLIT_VERTICAL;
            original_first = true;
            break;
        case SDLK_RIGHT:
            split_dir = SPLIT_VERTICAL;
            original_first = false;
            break;
        case SDLK_UP:
            split_dir = SPLIT_HORIZONTAL;
            original_first = true;
            break;
        case SDLK_DOWN:
            split_dir = SPLIT_HORIZONTAL;
            original_first = false;
            break;
        default:
            break;
        }
        if (split_dir != SPLIT_NONE) {
            uint8_t new_vp;
            if (viewport_bsp_split(&ed->vp_bsp,
                                    ed->vp_bsp.focused_viewport,
                                    split_dir, original_first, &new_vp)) {
                viewport_state_init(&ed->viewports[new_vp]);
                viewport_state_copy_camera(&ed->viewports[new_vp], fvp);
            }
            return true;
        }
    }

    /* Gizmo nudge: arrow keys apply exact rotation steps when an axis
     * is active (either during drag or after selecting an axis). */
    if (fvp->gizmo.active_axis != GIZMO_AXIS_NONE &&
        fvp->gizmo.mode == GIZMO_MODE_ROTATE) {
        float step_deg = 0.0f;
        if (key == SDLK_UP)   step_deg =  15.0f;
        if (key == SDLK_DOWN) step_deg = -15.0f;
        if (step_deg != 0.0f) {
            /* Get oriented axis from gizmo orientation matrix. */
            int axis_col = -1;
            switch (fvp->gizmo.active_axis) {
            case GIZMO_AXIS_X: axis_col = 0; break;
            case GIZMO_AXIS_Y: axis_col = 1; break;
            case GIZMO_AXIS_Z: axis_col = 2; break;
            default: break;
            }
            vec3_t axis_dir = {0, 1, 0};
            if (axis_col >= 0) {
                const mat4_t *o = &fvp->gizmo.orientation;
                axis_dir.x = o->m[axis_col * 4 + 0];
                axis_dir.y = o->m[axis_col * 4 + 1];
                axis_dir.z = o->m[axis_col * 4 + 2];
            }
            quat_t dq = quat_from_axis_angle(
                axis_dir, step_deg * INPUT_DEG_TO_RAD, 1e-8f);
            apply_gizmo_rotate(ed, dq);
            fvp->gizmo_rot_accum = quat_normalize_safe(
                quat_mul(dq, fvp->gizmo_rot_accum), 1e-8f);
            /* If not mid-drag, snap then send so server gets abs orientation. */
            if (!fvp->gizmo.dragging) {
                snap_selected_euler_axes(ed);
                vec3_t unused = {0, 0, 0};
                send_gizmo_commands(ed, unused);
            }
            return true;
        }
    }

    /* Unified scroll keys: scroll whichever panel is focused. */
    {
        int *scroll_ptr = NULL;
        int max_scroll = 0;
        int step = 1;       /* lines for outliner/tui, pixels for inspector */
        int page_step = 1;

        switch (ed->layout.focus) {
        case PANEL_OUTLINER:
            scroll_ptr = &ed->ui.outliner_scroll;
            max_scroll = ed->ui.outliner_total - ed->ui.outliner_visible_lines;
            step = 1;
            page_step = ed->ui.outliner_visible_lines;
            break;
        case PANEL_INSPECTOR:
            scroll_ptr = &ed->ui.inspector_scroll;
            max_scroll = ed->ui.inspector_total - ed->ui.inspector_visible_lines;
            step = THEME_ROW_HEIGHT;
            page_step = ed->ui.inspector_visible_lines;
            break;
        case PANEL_TUI:
            scroll_ptr = &ed->ui.tui_log_scroll;
            max_scroll = ed->ui.tui_log_count - ed->ui.tui_log_visible;
            step = -1;          /* Inverted: scroll value counts back from newest. */
            page_step = -(ed->ui.tui_log_visible > 0 ? ed->ui.tui_log_visible : 1);
            break;
        default:
            break;
        }

        if (max_scroll < 0) max_scroll = 0;

        if (scroll_ptr) {
            bool handled = true;
            switch (key) {
            case SDLK_UP:
                *scroll_ptr -= step;
                break;
            case SDLK_DOWN:
                *scroll_ptr += step;
                break;
            case SDLK_PAGEUP:
                *scroll_ptr -= page_step;
                break;
            case SDLK_PAGEDOWN:
                *scroll_ptr += page_step;
                break;
            case SDLK_HOME:
                *scroll_ptr = 0;
                break;
            case SDLK_END:
                *scroll_ptr = max_scroll;
                break;
            default:
                handled = false;
                break;
            }
            if (handled) {
                if (*scroll_ptr < 0) *scroll_ptr = 0;
                if (*scroll_ptr > max_scroll) *scroll_ptr = max_scroll;
                return true;
            }
        }
    }

    /* Panel toggles: F5=Outliner, F6=Viewport, F7=Inspector, F8=TUI */
    switch (key) {
    case SDLK_F5:
        panel_layout_toggle(&ed->layout, PANEL_OUTLINER);
        return true;
    case SDLK_F6:
        panel_layout_toggle(&ed->layout, PANEL_VIEWPORT);
        return true;
    case SDLK_F7:
        panel_layout_toggle(&ed->layout, PANEL_INSPECTOR);
        return true;
    case SDLK_F8:
        panel_layout_toggle(&ed->layout, PANEL_TUI);
        return true;
    case SDLK_F11:
        /* Toggle fullscreen */
        return true;
    case SDLK_TAB:
        if (ev->keysym.mod & KMOD_SHIFT) {
            panel_layout_focus_prev(&ed->layout);
        } else {
            panel_layout_focus_next(&ed->layout);
        }
        /* Activate TUI input when TUI receives focus. */
        ed->ui.tui_active = (ed->layout.focus == PANEL_TUI);
        return true;
    case SDLK_ESCAPE: {
        /* Exit prefab mode if active. */
        if (ed->prefab_mode.active) {
            prefab_mode_exit(ed);
            scene_ui_tui_log(&ed->ui, "Prefab mode: OFF");
            return true;
        }
        /* Close the focused viewport if multiple exist.
         * The last remaining viewport cannot be closed. */
        uint8_t fvp_idx = ed->vp_bsp.focused_viewport;
        if (ed->vp_bsp.viewport_count > 1) {
            if (viewport_bsp_close(&ed->vp_bsp, fvp_idx)) {
                viewport_state_t *cvs = &ed->viewports[fvp_idx];
                /* Viewport 0 shares the renderer's FBO — only delete
                 * FBO resources for viewports that own their own. */
                if (fvp_idx != 0 && cvs->fbo_valid) {
                    viewport_render_state_t *rend = &ed->viewport;
                    rend->glDeleteFramebuffers(1, &cvs->fbo);
                    rend->glDeleteTextures(1, &cvs->color_tex);
                    rend->glDeleteRenderbuffers(1, &cvs->depth_rbo);
                }
                viewport_state_init(cvs);
                cvs->active = false;
            }
            return true;
        }
        ed->ui.tui_active = false;
        panel_layout_focus_viewport(&ed->layout);
        return true;
    }

    /* '^': swap focused viewport with previously focused. */
    case SDLK_CARET: {
        uint8_t prev = ed->prev_focused_vp;
        if (prev != ed->vp_bsp.focused_viewport &&
            ed->viewports[prev].active) {
            ed->prev_focused_vp = ed->vp_bsp.focused_viewport;
            ed->vp_bsp.focused_viewport = prev;
        }
        return true;
    }

    /* Ctrl+Z: undo / Ctrl+Shift+Z: redo.
     * Check local bone undo stack first; if empty, send to server. */
    case SDLK_z: {
        if (!(ev->keysym.mod & KMOD_CTRL)) return false;
        bool is_redo = (ev->keysym.mod & KMOD_SHIFT) != 0;

        /* Try local bone undo first. */
        if (!is_redo && edit_undo_count(&ed->bone_undo) > 0) {
            uint32_t bc = ed->bone_undo.cursor;
            uint32_t undone = edit_undo_step(&ed->bone_undo);
            uint32_t ba = ed->bone_undo.cursor;
            for (uint32_t bi = bc; bi > ba; bi--) {
                uint32_t bidx = (bi - 1) % ed->bone_undo.capacity;
                const edit_undo_entry_t *be = &ed->bone_undo.entries[bidx];
                edit_undo_apply_bone_inverse(
                    &ed->entities, &ed->skeleton_registry,
                    &ed->bone_poses, ed->prefab_mode.active, be);
            }
            if (undone > 0) {
                scene_ui_tui_log(&ed->ui, "Bone undo");
                return true;
            }
        }
        if (is_redo && edit_undo_redo_count(&ed->bone_undo) > 0) {
            uint32_t bc = ed->bone_undo.cursor;
            uint32_t redone = edit_undo_redo(&ed->bone_undo);
            uint32_t ba = ed->bone_undo.cursor;
            for (uint32_t bi = bc; bi < ba; bi++) {
                uint32_t bidx = bi % ed->bone_undo.capacity;
                const edit_undo_entry_t *be = &ed->bone_undo.entries[bidx];
                edit_undo_apply_bone_forward(
                    &ed->entities, &ed->skeleton_registry,
                    &ed->bone_poses, ed->prefab_mode.active, be);
            }
            if (redone > 0) {
                scene_ui_tui_log(&ed->ui, "Bone redo");
                return true;
            }
        }

        /* No local bone undo — send to server for entity undo. */
        strncpy(ed->ui.tui_cmd, is_redo ? "redo" : "undo",
                UI_TUI_INPUT_MAX - 1);
        ed->ui.tui_cmd[UI_TUI_INPUT_MAX - 1] = '\0';
        ed->ui.action = UI_ACTION_TUI_COMMAND;
        return true;
    }

    /* Ctrl+S: save prefab (prefab mode) or bone poses (regular mode). */
    case SDLK_s: {
        if (!(ev->keysym.mod & KMOD_CTRL)) return false;
        if (!ed->prefab_mode.active) {
            /* Regular mode: save any per-entity bone pose overrides. */
            uint32_t saved = 0;
            for (uint32_t si = 0; si < ed->bone_poses.block_cap; si++) {
                const bone_pose_block_t *bp = &ed->bone_poses.blocks[si];
                if (!bp->active) continue;
                uint32_t eid = bp->entity_id;
                const edit_entity_t *se_ent =
                    edit_entity_store_get(&ed->entities, eid);
                if (!se_ent || !se_ent->active) continue;

                /* Build save path: asset_dir/bone_poses/<entity_name>.bpose */
                uint8_t nat = 0, nas = 0;
                const void *nv = entity_attrs_get(&se_ent->attrs,
                    SCRIPT_KEY_NAME, &nat, &nas);
                const char *ename = (nv && nat == SCRIPT_ATTR_STR)
                    ? (const char *)nv : "entity";
                char bpose_path[512];
                snprintf(bpose_path, sizeof(bpose_path),
                         "%s/bone_poses/%s_%u.bpose",
                         ed->config.asset_dir, ename, eid);

                if (bone_pose_file_write(bpose_path, bp)) {
                    /* Store the path on the entity as an attribute. */
                    char rel_path[256];
                    snprintf(rel_path, sizeof(rel_path),
                             "bone_poses/%s_%u.bpose", ename, eid);
                    entity_attrs_set((entity_attrs_t *)&se_ent->attrs,
                                      SCRIPT_KEY_BONE_POSE_PATH,
                                      SCRIPT_ATTR_STR,
                                      rel_path,
                                      (uint8_t)(strlen(rel_path) + 1));
                    saved++;
                }
            }
            if (saved > 0) {
                char msg[128];
                snprintf(msg, sizeof(msg),
                         "Saved %u bone pose override(s)", saved);
                scene_ui_tui_log(&ed->ui, msg);
            } else {
                scene_ui_tui_log(&ed->ui,
                    "No bone pose overrides to save");
            }
            return true;
        }

        /* Determine bone count from skeleton (if any). */
        uint32_t bone_count = 0;
        uint32_t root_id = ed->prefab_mode.root_entity_id;
        const edit_skeleton_entry_t *se = NULL;
        const edit_entity_t *root_ent =
            edit_entity_store_get(&ed->entities, root_id);
        if (root_ent) {
            uint8_t sat = 0, sas = 0;
            const void *ssp = entity_attrs_get(&root_ent->attrs,
                SCRIPT_KEY_SKEL_PATH, &sat, &sas);
            if (ssp && sat == SCRIPT_ATTR_STR) {
                const char *sp = (const char *)ssp;
                const char *fn = sp;
                for (const char *p = sp; *p; p++) {
                    if (*p == '/') fn = p + 1;
                }
                se = edit_skeleton_registry_get(&ed->skeleton_registry, fn);
                if (se) bone_count = se->skel.joint_count;
            }
        }

        /* Collect prefab data from entity store. */
        static prefab_def_t s_save_def;
        if (!prefab_collect_from_entities(&s_save_def, &ed->entities,
                                           root_id, bone_count)) {
            scene_ui_tui_log(&ed->ui, "Failed to collect prefab data");
            return true;
        }

        /* Capture per-bone rest_local overrides into the prefab.
         * These are stored in the fpfab, NOT in the fskel. */
        if (se && bone_count > 0) {
            uint32_t pose_n = bone_count < PREFAB_MAX_BONES
                            ? bone_count : PREFAB_MAX_BONES;
            s_save_def.bone_pose_count = pose_n;
            for (uint32_t bi = 0; bi < pose_n; bi++) {
                memcpy(s_save_def.bone_rest_local[bi],
                       se->skel.rest_local[bi].m,
                       16 * sizeof(float));
            }
        } else {
            s_save_def.bone_pose_count = 0;
        }

        /* Build save path: use existing fpfab_path or derive from name. */
        char save_path[512];
        if (ed->prefab_mode.fpfab_path[0] != '\0') {
            snprintf(save_path, sizeof(save_path), "%s",
                     ed->prefab_mode.fpfab_path);
        } else {
            /* Default: asset_dir/prefabs/<name>.fpfab */
            const char *name = ed->prefab_mode.name[0]
                ? ed->prefab_mode.name : "untitled";
            snprintf(save_path, sizeof(save_path), "%s/prefabs/%s.fpfab",
                     ed->config.asset_dir, name);
        }

        if (prefab_save(save_path, &s_save_def)) {
            /* Store path for future saves. */
            strncpy(ed->prefab_mode.fpfab_path, save_path,
                    sizeof(ed->prefab_mode.fpfab_path) - 1);
            ed->prefab_mode.fpfab_path[sizeof(ed->prefab_mode.fpfab_path) - 1]
                = '\0';
            ed->prefab_mode.dirty = false;

            char msg[128];
            snprintf(msg, sizeof(msg), "Saved prefab: %s", save_path);
            scene_ui_tui_log(&ed->ui, msg);
        } else {
            scene_ui_tui_log(&ed->ui, "Failed to save prefab");
        }
        return true;
    }

    /* ':' activates TUI command input (vim-style). */
    case SDLK_SEMICOLON:
        if (ev->keysym.mod & KMOD_SHIFT) {
            /* Shift+; = ':' on US keyboard layout.
             * Start text input so SDL sends TEXTINPUT events, then
             * suppress the ':' character that SDL_TEXTINPUT will
             * generate this frame by setting a skip flag. */
            ed->ui.tui_active = true;
            ed->ui.tui_skip_next_text = true;
            panel_layout_set_focus(&ed->layout, PANEL_TUI);
            SDL_StartTextInput();
            return true;
        }
        break;

    /* Entity operations. */
    case SDLK_DELETE:
    case SDLK_x:
        if (ev->keysym.mod & KMOD_SHIFT) {
            ed->ui.action = UI_ACTION_DELETE_SELECTED;
            return true;
        }
        if (key == SDLK_DELETE) {
            ed->ui.action = UI_ACTION_DELETE_SELECTED;
            return true;
        }
        break;

    /* Transform mode shortcuts (toggle: pressing same key disables gizmo).
     * W/E/R = Translate/Rotate/Scale (standard layout). */
    case SDLK_w:
        if (fvp->gizmo.mode == GIZMO_MODE_TRANSLATE) {
            ed->ui.action = UI_ACTION_MODE_NONE;
        } else {
            ed->ui.action = UI_ACTION_MODE_TRANSLATE;
        }
        return true;
    case SDLK_e:
        if (fvp->gizmo.mode == GIZMO_MODE_ROTATE) {
            ed->ui.action = UI_ACTION_MODE_NONE;
        } else {
            ed->ui.action = UI_ACTION_MODE_ROTATE;
        }
        return true;
    case SDLK_r:
        if (fvp->gizmo.mode == GIZMO_MODE_SCALE) {
            ed->ui.action = UI_ACTION_MODE_NONE;
        } else {
            ed->ui.action = UI_ACTION_MODE_SCALE;
        }
        return true;

    /* Toggle per-object gizmo mode: each selected entity gets its own gizmo. */
    case SDLK_t:
        fvp->per_object_gizmo = !fvp->per_object_gizmo;
        fvp->per_object_drag_entity = EDIT_ENTITY_INVALID_ID;
        scene_ui_tui_log(&ed->ui, fvp->per_object_gizmo
            ? "Per-object gizmo: ON" : "Per-object gizmo: OFF");
        return true;

    /* Cycle transform mode: Nav → Sel → Move → Rot → Scale. */
    case SDLK_PERIOD: {
        uint8_t next = (ed->ui.transform_mode + 1) % UI_MODE_COUNT;
        ed->ui.transform_mode = next;
        switch (next) {
        case UI_MODE_TRANSLATE:
            gizmo_state_set_mode(&fvp->gizmo, GIZMO_MODE_TRANSLATE); break;
        case UI_MODE_ROTATE:
            gizmo_state_set_mode(&fvp->gizmo, GIZMO_MODE_ROTATE); break;
        case UI_MODE_SCALE:
            gizmo_state_set_mode(&fvp->gizmo, GIZMO_MODE_SCALE); break;
        default:
            gizmo_state_set_mode(&fvp->gizmo, GIZMO_MODE_NONE); break;
        }
        return true;
    }

    /* Cycle transform basis: World → Local → View → Cursor. */
    case SDLK_COMMA: {
        fvp->gizmo.basis = transform_basis_next(fvp->gizmo.basis);
        char msg[64];
        snprintf(msg, sizeof(msg), "Basis: %s",
                 transform_basis_name(fvp->gizmo.basis));
        scene_ui_tui_log(&ed->ui, msg);
        return true;
    }

    /* Toggle collision wireframe overlay. */
    case SDLK_c:
        fvp->show_collision_wireframe = !fvp->show_collision_wireframe;
        scene_ui_tui_log(&ed->ui, fvp->show_collision_wireframe
            ? "Collision wireframe: ON" : "Collision wireframe: OFF");
        return true;

    /* Cycle viewport shading mode: Shaded → Matcap → Wire. */
    case SDLK_SLASH: {
        fvp->shading_mode = shading_mode_next(fvp->shading_mode);
        char msg[64];
        snprintf(msg, sizeof(msg), "Shading: %s",
                 shading_mode_name(fvp->shading_mode));
        scene_ui_tui_log(&ed->ui, msg);
        return true;
    }

    /* Cycle viewport navigation mode: Orbit → OrbCur → Fly → PanZm. */
    case SDLK_n: {
        nav_mode_t old_mode = fvp->nav_mode;
        nav_mode_t new_mode = nav_mode_next(old_mode);
        /* Handle transitions to/from fly mode. */
        if (old_mode == NAV_MODE_FLY && new_mode != NAV_MODE_FLY) {
            editor_camera_exit_fly(&fvp->camera, 10.0f);
        } else if (old_mode != NAV_MODE_FLY && new_mode == NAV_MODE_FLY) {
            editor_camera_enter_fly(&fvp->camera);
        }
        fvp->nav_mode = new_mode;
        char msg[64];
        snprintf(msg, sizeof(msg), "Nav: %s", nav_mode_name(new_mode));
        scene_ui_tui_log(&ed->ui, msg);
        return true;
    }

    /* Keyboard zoom: +/- (also = for unshifted plus). */
    case SDLK_PLUS:
    case SDLK_EQUALS:
        editor_camera_zoom(&fvp->camera, -CAMERA_ZOOM_SPEED);
        return true;
    case SDLK_MINUS:
        editor_camera_zoom(&fvp->camera, CAMERA_ZOOM_SPEED);
        return true;

    /* A: select all / Shift+A: deselect all / Ctrl+A: select all bones. */
    case SDLK_a: {
        SDL_Keymod km = SDL_GetModState();
        if (km & KMOD_CTRL) {
            /* Ctrl+A: select all bones if active entity has a skeleton. */
            if (ed->active_object_id != EDIT_ENTITY_INVALID_ID) {
                const edit_entity_t *ae = edit_entity_store_get(
                    &ed->entities, ed->active_object_id);
                if (ae && ae->active) {
                    uint8_t bat2 = 0, bas2 = 0;
                    const void *bsp2 = entity_attrs_get(
                        &ae->attrs, SCRIPT_KEY_SKEL_PATH, &bat2, &bas2);
                    if (bsp2 && bat2 == SCRIPT_ATTR_STR) {
                        const char *bp2 = (const char *)bsp2;
                        if (bp2[0] != '\0') {
                            const char *fn2 = bp2;
                            for (const char *p = bp2; *p; p++) {
                                if (*p == '/') fn2 = p + 1;
                            }
                            const edit_skeleton_entry_t *se2 =
                                edit_skeleton_registry_get(
                                    &ed->skeleton_registry, fn2);
                            if (se2 && se2->skel.joint_count > 0) {
                                edit_bone_selection_clear(&ed->bone_selection);
                                for (uint32_t bj = 0;
                                     bj < se2->skel.joint_count; bj++) {
                                    edit_bone_selection_add(
                                        &ed->bone_selection,
                                        ed->active_object_id, bj);
                                }
                            }
                        }
                    }
                }
            }
            return true;
        }
        if (km & KMOD_SHIFT) {
            /* Shift+A: deselect all — sync each to server. */
            uint32_t sc = edit_selection_count(&ed->selection);
            const uint32_t *sids = edit_selection_ids(&ed->selection);
            for (uint32_t si = 0; si < sc; ++si) {
                char buf[256];
                uint32_t cid = scene_connection_next_id(&ed->connection);
                int n = scene_cmd_format_deselect(buf, sizeof(buf),
                                                   cid, sids[si]);
                if (n > 0 && ed->connected) {
                    ctrl_conn_send_raw(&ed->connection.tcp,
                                       buf, (uint32_t)n);
                }
            }
            edit_selection_clear(&ed->selection);
            ed->ui.pivot_edit_mode = false;
            ed->active_object_id = EDIT_ENTITY_INVALID_ID;
        } else {
            /* A: select all non-deleted entities — sync each to server.
             * Multi-selection exits pivot mode. */
            ed->ui.pivot_edit_mode = false;
            for (uint32_t i = 0; i < ed->entities.capacity; ++i) {
                const edit_entity_t *ent =
                    edit_entity_store_get(&ed->entities, i);
                if (ent && !ent->pending_delete) {
                    if (!edit_selection_contains(&ed->selection, i)) {
                        char buf[256];
                        uint32_t cid =
                            scene_connection_next_id(&ed->connection);
                        int n = scene_cmd_format_select(buf, sizeof(buf),
                                                        cid, i);
                        if (n > 0 && ed->connected) {
                            ctrl_conn_send_raw(&ed->connection.tcp,
                                               buf, (uint32_t)n);
                        }
                    }
                    edit_selection_add(&ed->selection, i);
                }
            }
        }
        return true;
    }

    /* D: duplicate selected entities via per-entity clone_id.
     * clone_id returns a delta sync response, so new entities
     * appear in the viewport immediately when the response arrives.
     * Deselect originals so only the clones end up selected. */
    case SDLK_d: {
        uint32_t sel_count = edit_selection_count(&ed->selection);
        if (sel_count > 0) {
            const uint32_t *sel_ids = edit_selection_ids(&ed->selection);
            uint32_t last_cid = 0;
            for (uint32_t si = 0; si < sel_count; ++si) {
                char buf[256];
                uint32_t cid = scene_connection_next_id(&ed->connection);
                int n = snprintf(buf, sizeof(buf),
                    "{\"id\":%u,\"cmd\":\"clone_id\",\"args\":"
                    "{\"entity_id\":%u}}\n",
                    (unsigned)cid, (unsigned)sel_ids[si]);
                if (n > 0 && (size_t)n < sizeof(buf)) {
                    scene_connection_send_cmd(&ed->connection, buf);
                }
                last_cid = cid;
            }
            /* Deselect originals — clones will be selected when the
             * delta sync response arrives and is processed. */
            edit_selection_clear(&ed->selection);
            scene_ui_tui_log_pending(&ed->ui, "clone", last_cid);
        }
        return true;
    }

    /* P: toggle prefab mode / Shift+P: toggle pivot edit mode. */
    case SDLK_p: {
        SDL_Keymod mod = SDL_GetModState();
        if (mod & KMOD_SHIFT) {
            if (ed->ui.pivot_edit_mode) {
                ed->ui.pivot_edit_mode = false;
                scene_ui_tui_log(&ed->ui, "Pivot mode: OFF");
            } else if (edit_selection_count(&ed->selection) == 1) {
                ed->ui.pivot_edit_mode = true;
                scene_ui_tui_log(&ed->ui, "Pivot mode: ON");
            } else {
                scene_ui_tui_log(&ed->ui,
                                   "Pivot mode requires single selection");
            }
        } else {
            /* Bare P: toggle prefab editor mode. */
            if (ed->prefab_mode.active) {
                prefab_mode_exit(ed);
                scene_ui_tui_log(&ed->ui, "Prefab mode: OFF");
            } else {
                if (prefab_mode_enter(ed)) {
                    char pmsg[320];
                    snprintf(pmsg, sizeof(pmsg), "Prefab mode: %s",
                             ed->prefab_mode.name);
                    scene_ui_tui_log(&ed->ui, pmsg);
                } else {
                    scene_ui_tui_log(&ed->ui,
                        "Prefab mode requires a skeleton entity");
                }
            }
        }
        return true;
    }

    case SDLK_h: {
        SDL_Keymod mod = SDL_GetModState();
        if (mod & KMOD_SHIFT) {
            /* Shift+H: unhide selected entities. */
            uint32_t sel_count = edit_selection_count(&ed->selection);
            if (sel_count > 0) {
                const uint32_t *sel_ids =
                    edit_selection_ids(&ed->selection);
                for (uint32_t si = 0; si < sel_count; ++si) {
                    edit_entity_t *ent = edit_entity_store_get_mut(
                        &ed->entities, sel_ids[si]);
                    if (ent) ent->hidden = false;
                }
                scene_ui_tui_log(&ed->ui, "Show selected");
            }
        } else {
            /* H: hide selected entities. */
            uint32_t sel_count = edit_selection_count(&ed->selection);
            if (sel_count > 0) {
                const uint32_t *sel_ids =
                    edit_selection_ids(&ed->selection);
                for (uint32_t si = 0; si < sel_count; ++si) {
                    edit_entity_t *ent = edit_entity_store_get_mut(
                        &ed->entities, sel_ids[si]);
                    if (ent) ent->hidden = true;
                }
                edit_selection_clear(&ed->selection);
                scene_ui_tui_log(&ed->ui, "Hide selected");
            }
        }
        return true;
    }

    /* Arrow keys: fly mode movement (forward/back/left/right). */
    case SDLK_UP:
        if (fvp->nav_mode == NAV_MODE_FLY) {
            editor_camera_fly_move(&fvp->camera, CAMERA_FLY_SPEED, 0, 0);
            return true;
        }
        break;
    case SDLK_DOWN:
        if (fvp->nav_mode == NAV_MODE_FLY) {
            editor_camera_fly_move(&fvp->camera, -CAMERA_FLY_SPEED, 0, 0);
            return true;
        }
        break;
    case SDLK_LEFT:
        if (fvp->nav_mode == NAV_MODE_FLY) {
            editor_camera_fly_move(&fvp->camera, 0, -CAMERA_FLY_SPEED, 0);
            return true;
        }
        break;
    case SDLK_RIGHT:
        if (fvp->nav_mode == NAV_MODE_FLY) {
            editor_camera_fly_move(&fvp->camera, 0, CAMERA_FLY_SPEED, 0);
            return true;
        }
        break;

    /* Snap view shortcuts (number row keys). */
    case SDLK_1:
        if (ev->keysym.mod & KMOD_CTRL) {
            editor_camera_snap_view(&fvp->camera, EDITOR_VIEW_BACK);
        } else {
            editor_camera_snap_view(&fvp->camera, EDITOR_VIEW_FRONT);
        }
        return true;
    case SDLK_3:
        if (ev->keysym.mod & KMOD_CTRL) {
            editor_camera_snap_view(&fvp->camera, EDITOR_VIEW_LEFT);
        } else {
            editor_camera_snap_view(&fvp->camera, EDITOR_VIEW_RIGHT);
        }
        return true;
    case SDLK_7:
        if (ev->keysym.mod & KMOD_CTRL) {
            editor_camera_snap_view(&fvp->camera, EDITOR_VIEW_BOTTOM);
        } else {
            editor_camera_snap_view(&fvp->camera, EDITOR_VIEW_TOP);
        }
        return true;
    case SDLK_5:
        editor_camera_toggle_projection(&fvp->camera);
        return true;

    /* Incremental orbit (15-degree steps). */
    case SDLK_8:
        editor_camera_orbit(&fvp->camera,
                             0.0f, 15.0f * INPUT_DEG_TO_RAD);
        return true;
    case SDLK_2:
        editor_camera_orbit(&fvp->camera,
                             0.0f, -15.0f * INPUT_DEG_TO_RAD);
        return true;
    case SDLK_4:
        editor_camera_orbit(&fvp->camera,
                             -15.0f * INPUT_DEG_TO_RAD, 0.0f);
        return true;
    case SDLK_6:
        editor_camera_orbit(&fvp->camera,
                             15.0f * INPUT_DEG_TO_RAD, 0.0f);
        return true;
    case SDLK_9:
        /* Flip view 180° around yaw. */
        editor_camera_orbit(&fvp->camera,
                             180.0f * INPUT_DEG_TO_RAD, 0.0f);
        return true;
    case SDLK_f: {
        /* Frame selection: compute AABB and center camera on it. */
        vec3_t aabb_min = { 1e30f,  1e30f,  1e30f};
        vec3_t aabb_max = {-1e30f, -1e30f, -1e30f};
        bool have_bounds = false;

        /* If bones are selected, frame the selected bones. */
        uint32_t bone_count_f = 0;
        const uint32_t *bone_indices_f = edit_bone_selection_bones(
            &ed->bone_selection, &bone_count_f);
        if (bone_count_f > 0 &&
            ed->bone_selection.entity_id != EDIT_BONE_SEL_NONE) {
            uint32_t beid = ed->bone_selection.entity_id;
            const edit_entity_t *bent =
                edit_entity_store_get(&ed->entities, beid);
            if (bent) {
                /* Get skeleton for bone positions. */
                uint8_t bsat = 0, bsas = 0;
                const void *bsp = entity_attrs_get(&bent->attrs,
                    SCRIPT_KEY_SKEL_PATH, &bsat, &bsas);
                const edit_skeleton_entry_t *bse = NULL;
                if (bsp && bsat == SCRIPT_ATTR_STR)
                    bse = edit_skeleton_registry_get(
                        &ed->skeleton_registry, (const char *)bsp);
                if (bse) {
                    /* Use per-entity pose override if available. */
                    const mat4_t *f_rest_world = bse->skel.rest_world;
                    const bone_pose_block_t *f_bp =
                        bone_pose_store_get(&ed->bone_poses, beid);
                    if (f_bp) f_rest_world = f_bp->rest_world;

                    /* Build entity model matrix. */
                    mat4_t em = edit_entity_build_model_matrix(bent);

                    for (uint32_t bi = 0; bi < bone_count_f; bi++) {
                        uint32_t idx = bone_indices_f[bi];
                        if (idx >= bse->skel.joint_count) continue;
                        /* Bone head in skeleton-local space. */
                        float bx = f_rest_world[idx].m[12];
                        float by = f_rest_world[idx].m[13];
                        float bz = f_rest_world[idx].m[14];
                        /* Transform to world space. */
                        float wx = em.m[0]*bx + em.m[4]*by + em.m[8]*bz + em.m[12];
                        float wy = em.m[1]*bx + em.m[5]*by + em.m[9]*bz + em.m[13];
                        float wz = em.m[2]*bx + em.m[6]*by + em.m[10]*bz + em.m[14];
                        if (wx < aabb_min.x) aabb_min.x = wx;
                        if (wy < aabb_min.y) aabb_min.y = wy;
                        if (wz < aabb_min.z) aabb_min.z = wz;
                        if (wx > aabb_max.x) aabb_max.x = wx;
                        if (wy > aabb_max.y) aabb_max.y = wy;
                        if (wz > aabb_max.z) aabb_max.z = wz;
                    }
                    /* Add a small padding so single-bone doesn't have zero AABB. */
                    float pad = 0.5f;
                    aabb_min.x -= pad; aabb_min.y -= pad; aabb_min.z -= pad;
                    aabb_max.x += pad; aabb_max.y += pad; aabb_max.z += pad;
                    have_bounds = true;
                }
            }
        }

        /* Fall back to entity selection. */
        if (!have_bounds && edit_selection_count(&ed->selection) > 0) {
            uint32_t sel_count = edit_selection_count(&ed->selection);
            const uint32_t *sel_ids = edit_selection_ids(&ed->selection);
            for (uint32_t si = 0; si < sel_count; si++) {
                const edit_entity_t *ent =
                    edit_entity_store_get(&ed->entities, sel_ids[si]);
                if (!ent) continue;
                float gc[3];
                edit_entity_geometry_center(ent, gc);
                float hx = fabsf(ent->scale[0]) * 0.5f;
                float hy = fabsf(ent->scale[1]) * 0.5f;
                float hz = fabsf(ent->scale[2]) * 0.5f;
                if (gc[0] - hx < aabb_min.x) aabb_min.x = gc[0] - hx;
                if (gc[1] - hy < aabb_min.y) aabb_min.y = gc[1] - hy;
                if (gc[2] - hz < aabb_min.z) aabb_min.z = gc[2] - hz;
                if (gc[0] + hx > aabb_max.x) aabb_max.x = gc[0] + hx;
                if (gc[1] + hy > aabb_max.y) aabb_max.y = gc[1] + hy;
                if (gc[2] + hz > aabb_max.z) aabb_max.z = gc[2] + hz;
            }
            have_bounds = true;
        }

        if (have_bounds) {
            editor_camera_frame_selection(&fvp->camera, aabb_min, aabb_max);
        }
        return true;
    }

    /* G: toggle snap for the current gizmo mode.
     * Ctrl+G: toggle all snap types at once. */
    case SDLK_g: {
        if (ev->keysym.mod & KMOD_ALT) {
            /* Alt+G: reset pivot offset to center for all selected.
             * Adjusts pos so geometry stays in place, then sends
             * pivot_id commands to the server. */
            const uint32_t *sel_ids = edit_selection_ids(&ed->selection);
            uint32_t sel_count = edit_selection_count(&ed->selection);
            for (uint32_t si = 0; si < sel_count; ++si) {
                edit_entity_t *ent = edit_entity_store_get_mut(
                    &ed->entities, sel_ids[si]);
                if (!ent) continue;
                /* pos_new = pos_old + R*S*(0 - old_pivot) = pos_old - R*S*old_pivot */
                vec3_t scaled = {
                    -ent->pivot_offset[0] * ent->scale[0],
                    -ent->pivot_offset[1] * ent->scale[1],
                    -ent->pivot_offset[2] * ent->scale[2],
                };
                vec3_t adj = quat_rotate_vec3(ent->orientation, scaled);
                ent->pos[0] += adj.x;
                ent->pos[1] += adj.y;
                ent->pos[2] += adj.z;
                ent->pivot_offset[0] = 0.0f;
                ent->pivot_offset[1] = 0.0f;
                ent->pivot_offset[2] = 0.0f;
                /* Send pivot_id to server. */
                char buf[256];
                uint32_t cid = scene_connection_next_id(&ed->connection);
                int n = snprintf(buf, sizeof(buf),
                    "{\"id\":%u,\"cmd\":\"pivot_id\",\"args\":{"
                    "\"entity_id\":%u,\"pivot\":[0,0,0]}}",
                    cid, sel_ids[si]);
                if (n > 0 && (size_t)n < sizeof(buf)) {
                    scene_connection_send_cmd(&ed->connection, buf);
                }
            }
            scene_ui_tui_log(&ed->ui, "Pivot reset to center");
            return true;
        }
        snap_state_t *sn = &ed->snap;
        if (ev->keysym.mod & KMOD_SHIFT) {
            /* Shift+G: enable all three snaps, unless all three are
             * already on — in that case, disable all three. */
            bool all_on = sn->enabled[SNAP_POSITION] &&
                          sn->enabled[SNAP_ROTATION] &&
                          sn->enabled[SNAP_SCALE];
            bool new_val = !all_on;
            sn->enabled[SNAP_POSITION] = new_val;
            sn->enabled[SNAP_ROTATION] = new_val;
            sn->enabled[SNAP_SCALE]    = new_val;
            /* When enabling, immediately snap selected entities' eulers
             * and sync per-entity absolute orientation to server. */
            if (new_val) {
                snap_selected_euler_axes(ed);
                send_per_entity_abs_(ed, GIZMO_MODE_ROTATE);
            }
            char msg[64];
            snprintf(msg, sizeof(msg), "Snap: %s", new_val ? "ALL ON" : "OFF");
            scene_ui_tui_log(&ed->ui, msg);
        } else {
            /* G: toggle snap for the current gizmo mode. */
            snap_transform_type_t st = SNAP_POSITION;
            const char *type_name = "Position";
            switch (fvp->gizmo.mode) {
            case GIZMO_MODE_ROTATE:
                st = SNAP_ROTATION;
                type_name = "Rotation";
                break;
            case GIZMO_MODE_SCALE:
                st = SNAP_SCALE;
                type_name = "Scale";
                break;
            default:
                break;
            }
            sn->enabled[st] = !sn->enabled[st];
            /* When enabling rotation snap, immediately snap eulers
             * and sync per-entity absolute orientation to server. */
            if (sn->enabled[st] && st == SNAP_ROTATION) {
                snap_selected_euler_axes(ed);
                send_per_entity_abs_(ed, GIZMO_MODE_ROTATE);
            }
            char msg[64];
            snprintf(msg, sizeof(msg), "%s snap: %s (grid=%.2g)",
                     type_name,
                     sn->enabled[st] ? "ON" : "OFF",
                     (double)sn->grid_size[st]);
            scene_ui_tui_log(&ed->ui, msg);
        }
        return true;
    }

    default:
        break;
    }

    return false;
}

/* ---- Public API ---- */

bool scene_input_process(struct scene_editor *ed, const union SDL_Event *event) {
    switch (event->type) {
    case SDL_QUIT:
        ed->running = false;
        return true;

    case SDL_WINDOWEVENT:
        if (event->window.event == SDL_WINDOWEVENT_RESIZED ||
            event->window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
            /* Layout uses logical pixels (physical / ui_scale).
             * render_frame() calls panel_layout_resize() each frame
             * with the correct logical dimensions, so skip it here
             * to avoid a one-frame mismatch with physical sizes. */
            return true;
        }
        break;

    case SDL_MOUSEBUTTONDOWN:
        return handle_mouse_down(ed, &event->button);

    case SDL_MOUSEBUTTONUP:
        return handle_mouse_up(ed, &event->button);

    case SDL_MOUSEMOTION:
        return handle_mouse_motion(ed, &event->motion);

    case SDL_MOUSEWHEEL: {
        /* Route scroll wheel based on which panel the mouse is over. */
        float sc = ed->clay_be.ui_scale;
        if (sc < 1.0f) sc = 1.0f;
        int lx = (int)(ed->ui.mouse_x / sc);
        int ly = (int)(ed->ui.mouse_y / sc);
        panel_id_t hover_panel = panel_layout_hit_test(&ed->layout, lx, ly);
        if (hover_panel == PANEL_VIEWPORT) {
            viewport_state_t *fvp = scene_focused_vp(ed);
            editor_camera_zoom(&fvp->camera,
                                -(float)event->wheel.y * CAMERA_ZOOM_SPEED);
        } else if (hover_panel == PANEL_TUI) {
            /* Scroll TUI log: wheel up = scroll back, wheel down = toward newest. */
            int max_scroll = ed->ui.tui_log_count - ed->ui.tui_log_visible;
            if (max_scroll < 0) max_scroll = 0;
            ed->ui.tui_log_scroll += event->wheel.y;
            if (ed->ui.tui_log_scroll < 0) ed->ui.tui_log_scroll = 0;
            if (ed->ui.tui_log_scroll > max_scroll)
                ed->ui.tui_log_scroll = max_scroll;
        } else if (hover_panel == PANEL_OUTLINER) {
            /* Outliner panel is split: top 60% entity list, bottom 40% asset browser.
             * Route scroll to whichever sub-panel the mouse is over. */
            panel_rect_t outliner_r = panel_layout_get_rect(&ed->layout, PANEL_OUTLINER);
            int split_y = outliner_r.y + (outliner_r.h * 3) / 5;

            if (ly >= split_y) {
                /* Asset browser scroll. */
                int max_scroll = ed->ui.asset_browser_total
                                 - ed->ui.asset_browser_visible_lines;
                if (max_scroll < 0) max_scroll = 0;
                ed->ui.asset_browser_scroll -= event->wheel.y;
                if (ed->ui.asset_browser_scroll < 0)
                    ed->ui.asset_browser_scroll = 0;
                if (ed->ui.asset_browser_scroll > max_scroll)
                    ed->ui.asset_browser_scroll = max_scroll;
            } else if (ed->prefab_mode.active) {
                /* Prefab outliner scroll. */
                int max_scroll = ed->ui.prefab_outliner_total
                                 - ed->ui.prefab_outliner_visible_lines;
                if (max_scroll < 0) max_scroll = 0;
                ed->ui.prefab_outliner_scroll -= event->wheel.y;
                if (ed->ui.prefab_outliner_scroll < 0)
                    ed->ui.prefab_outliner_scroll = 0;
                if (ed->ui.prefab_outliner_scroll > max_scroll)
                    ed->ui.prefab_outliner_scroll = max_scroll;
            } else {
                /* Outliner entity list scroll. */
                int max_scroll = ed->ui.outliner_total
                                 - ed->ui.outliner_visible_lines;
                if (max_scroll < 0) max_scroll = 0;
                ed->ui.outliner_scroll -= event->wheel.y;
                if (ed->ui.outliner_scroll < 0) ed->ui.outliner_scroll = 0;
                if (ed->ui.outliner_scroll > max_scroll)
                    ed->ui.outliner_scroll = max_scroll;
            }
        } else if (hover_panel == PANEL_INSPECTOR) {
            /* Scroll inspector (pixel-based). */
            int max_scroll = ed->ui.inspector_total
                             - ed->ui.inspector_visible_lines;
            if (max_scroll < 0) max_scroll = 0;
            ed->ui.inspector_scroll -= event->wheel.y * THEME_ROW_HEIGHT;
            if (ed->ui.inspector_scroll < 0) ed->ui.inspector_scroll = 0;
            if (ed->ui.inspector_scroll > max_scroll)
                ed->ui.inspector_scroll = max_scroll;
        } else {
            /* Other panels: pass to Clay scroll containers. */
            ed->ui.scroll_delta_y += (float)event->wheel.y * 40.0f;
        }
        return true;
    }

    case SDL_KEYDOWN:
        return handle_key_down(ed, &event->key);

    case SDL_KEYUP:
        key_mark_released(&ed->ui, (uint32_t)event->key.keysym.sym);
        return false;

    case SDL_TEXTINPUT:
        return handle_text_input(ed, event->text.text);

    default:
        break;
    }

    return false;
}
