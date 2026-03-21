/**
 * @file scene_viewport_draw.c
 * @brief Viewport 3D draw pass: render entities + grid into FBO.
 *
 * Binds the viewport FBO, clears, renders the grid, then iterates
 * all active entities and draws them with the appropriate mesh from
 * the mesh registry. Uses the existing renderer infrastructure
 * (shader_program_t, shader_uniform_cache_t, static_mesh_t, vao_t).
 *
 * Entity type → mesh mapping:
 *   BOX       → unit cube (built-in primitive)
 *   SPHERE    → unit sphere (built-in primitive)
 *   CAPSULE   → unit capsule (built-in primitive)
 *   HALFSPACE → large plane (built-in primitive)
 *   MESH      → loaded FVMA geometry from entity mesh cache
 *   MARKER    → small wireframe cross (rendered as 3 axis lines)
 *
 * Non-static functions (4 / 4 limit):
 *   viewport_render_draw_scene
 *   viewport_render_draw_grid
 *   viewport_render_draw_entities
 *   viewport_render_get_primitive_mesh
 */

#include "ferrum/editor/scene/scene_viewport_render.h"
#include "ferrum/editor/scene/scene_main.h"
#include "ferrum/editor/scene/viewport_bsp/viewport_bsp.h"
#include "ferrum/editor/scene/viewport_bsp/viewport_state.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_selection.h"
#include "ferrum/editor/viewport/transform_basis.h"
#include "ferrum/editor/viewport/viewport_shading.h"
#include "ferrum/editor/scene/scene_gizmo_per_object.h"
#include "ferrum/editor/scene/scene_gizmo_bone.h"
#include "ferrum/editor/scene/scene_viewport_bone_overlay.h"
#include "ferrum/editor/scene/prefab/prefab_hull_overlay.h"
#include "ferrum/editor/scene/prefab/prefab_hull_build.h"
#include "ferrum/editor/scene/prefab/prefab_hull_cache.h"
#include "ferrum/editor/edit_bone_selection.h"
#include "ferrum/editor/edit_skeleton_registry.h"
#include "ferrum/entity/entity_attrs.h"

#include "ferrum/renderer/gl_constants.h"
#include "ferrum/renderer/mesh/mesh_registry.h"
#include "ferrum/renderer/mesh/static_mesh.h"
#include "ferrum/renderer/mesh/skeletal_mesh.h"
#include "ferrum/editor/mesh/mesh_slot.h"
#include "ferrum/editor/mesh/mesh_vao_format.h"
#include "ferrum/math/mat4.h"
#include "ferrum/math/quat.h"
#include "ferrum/math/vec3.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* ---- Constants ---- */

/** Default light direction (upper-right-front, normalized). */
static const float LIGHT_DIR[3] = {0.577f, 0.577f, 0.577f};

/** Entity type colors (RGB, 0-1 range). */
static const float COLOR_BOX[3]       = {0.6f, 0.6f, 0.7f};
static const float COLOR_SPHERE[3]    = {0.5f, 0.7f, 0.6f};
static const float COLOR_CAPSULE[3]   = {0.7f, 0.5f, 0.6f};
static const float COLOR_HALFSPACE[3] = {0.4f, 0.4f, 0.4f};
static const float COLOR_MESH[3]      = {0.65f, 0.65f, 0.55f};
static const float COLOR_MARKER[3]    = {1.0f, 1.0f, 0.0f};

/* ---- Primitive mesh lookup ---- */

/**
 * @brief Look up the pre-registered primitive mesh for a given entity type.
 *
 * Primitives are registered in the mesh registry at init time by
 * viewport_render_init_primitives(). Returns NULL for MESH type
 * (those use loaded FVMA geometry from the entity mesh cache).
 *
 * @return Pointer to the static mesh, or NULL if type has no primitive.
 */
const static_mesh_t *viewport_render_get_primitive_mesh(
    uint32_t entity_type, const viewport_render_state_t *state) {
    mesh_handle_t handle;

    switch (entity_type) {
    case EDIT_ENTITY_TYPE_BOX:
    case EDIT_ENTITY_TYPE_COLLIDER_BOX:
        handle = state->mesh_box;
        break;
    case EDIT_ENTITY_TYPE_SPHERE:
    case EDIT_ENTITY_TYPE_COLLIDER_SPHERE:
        handle = state->mesh_sphere;
        break;
    case EDIT_ENTITY_TYPE_CAPSULE:
    case EDIT_ENTITY_TYPE_COLLIDER_CAPSULE:
        handle = state->mesh_capsule;
        break;
    case EDIT_ENTITY_TYPE_HALFSPACE:
        handle = state->mesh_plane;
        break;
    case EDIT_ENTITY_TYPE_MESH:
        return NULL;
    case EDIT_ENTITY_TYPE_MARKER:
    case EDIT_ENTITY_TYPE_COLLIDER_HULL:
        handle = state->mesh_sphere;
        break;
    default:
        handle = state->mesh_box;
        break;
    }

    return mesh_registry_get_static(&state->meshes, handle);
}

/**
 * @brief Get the base color for an entity type (no selection tinting).
 */
static const float *get_entity_color(uint32_t entity_type) {
    switch (entity_type) {
    case EDIT_ENTITY_TYPE_BOX:       return COLOR_BOX;
    case EDIT_ENTITY_TYPE_SPHERE:    return COLOR_SPHERE;
    case EDIT_ENTITY_TYPE_CAPSULE:   return COLOR_CAPSULE;
    case EDIT_ENTITY_TYPE_HALFSPACE: return COLOR_HALFSPACE;
    case EDIT_ENTITY_TYPE_MESH:      return COLOR_MESH;
    case EDIT_ENTITY_TYPE_MARKER:    return COLOR_MARKER;
    default:                         return COLOR_BOX;
    }
}

/** Selection tint color (orange). */
static const float SELECT_TINT_COLOR[3] = {1.0f, 0.55f, 0.1f};

/** Active object tint color (whitish-yellow). */
static const float ACTIVE_TINT_COLOR[3] = {1.0f, 0.9f, 0.5f};

/** No tint (0.0). */
static const float NO_TINT = 0.0f;

/** Selection tint amount — subtle but clearly visible. */
static const float SELECT_TINT_AMOUNT = 0.22f;

/** Active object tint amount — slightly stronger. */
static const float ACTIVE_TINT_AMOUNT = 0.30f;

/**
 * @brief Build a model matrix from entity position, orientation, scale,
 *        and pivot offset.
 *
 * Model matrix = T(pos) * R * S * T(-pivot_offset).
 * entity.pos is the pivot world position; geometry is offset by
 * -pivot_offset in local space so rotation/scale naturally happen
 * around the pivot.
 */
static mat4_t build_model_matrix(const edit_entity_t *ent) {
    mat4_t pivot_shift = mat4_translation(
        -ent->pivot_offset[0], -ent->pivot_offset[1],
        -ent->pivot_offset[2]);
    mat4_t scale = mat4_scaling(ent->scale[0], ent->scale[1], ent->scale[2]);
    mat4_t rot;
    quat_to_mat4(ent->orientation, &rot);
    mat4_t trans = mat4_translation(ent->pos[0], ent->pos[1], ent->pos[2]);

    /* T(pos) * R * S * T(-pivot_offset) */
    return mat4_mul(trans, mat4_mul(rot, mat4_mul(scale, pivot_shift)));
}

/* ---- Public API ---- */

void viewport_render_draw_grid(viewport_render_state_t *state,
                                const mat4_t *view, const mat4_t *proj) {
    if (!state || !state->initialized) return;

    /* Compute view-projection matrix for the grid. */
    mat4_t vp = mat4_mul(*proj, *view);

    /* Bind grid shader and set view-projection uniform. */
    shader_program_bind(&state->grid_shader);
    shader_uniform_set_mat4(&state->grid_uniforms, &state->grid_shader,
                             "u_vp", vp.m, GL_FALSE);

    /* Draw grid lines. */
    state->grid_vao.glBindVertexArray(state->grid_vao.handle);
    state->glDrawArrays(GL_LINES, 0, state->grid_vertex_count);
    state->grid_vao.glBindVertexArray(0);
}

void viewport_render_draw_entities(viewport_render_state_t *state,
                                    const edit_entity_store_t *entities,
                                    const edit_selection_t *selection,
                                    uint32_t active_object_id,
                                    const mat4_t *view, const mat4_t *proj,
                                    const vec3_t *eye_pos,
                                    shading_mode_t shading_mode,
                                    const edit_skeleton_registry_t *skel_reg,
                                    const bone_pose_store_t *bone_poses) {
    if (!state || !state->initialized || !entities) return;

    /* Select shader + uniform cache based on shading mode. */
    shader_program_t *shader;
    shader_uniform_cache_t *ucache;
    switch (shading_mode) {
    case SHADING_MODE_MATCAP:
        shader = &state->matcap_shader;
        ucache = &state->matcap_uniforms;
        break;
    case SHADING_MODE_UNLIT:
    case SHADING_MODE_WIREFRAME:
        shader = &state->flat_shader;
        ucache = &state->flat_uniforms;
        break;
    default: /* SHADING_MODE_SHADED */
        shader = &state->shader;
        ucache = &state->uniforms;
        break;
    }

    /* Bind selected shader and set per-frame uniforms. */
    shader_program_bind(shader);
    shader_uniform_set_mat4(ucache, shader, "u_view", view->m, GL_FALSE);
    shader_uniform_set_mat4(ucache, shader, "u_projection", proj->m, GL_FALSE);

    /* Light direction and eye position (not used by flat shader but
     * setting them is harmless — the uniforms just get ignored). */
    shader_uniform_set_vec3(ucache, shader, "u_light_dir", LIGHT_DIR);
    float eye[3] = {eye_pos->x, eye_pos->y, eye_pos->z};
    shader_uniform_set_vec3(ucache, shader, "u_eye_pos", eye);

    /* Initialize selection tint to zero (no tint). */
    shader_uniform_set_float(ucache, shader, "u_select_tint", NO_TINT);

    /* Wireframe: set polygon mode to lines. */
    if (shading_mode == SHADING_MODE_WIREFRAME && state->glPolygonMode) {
        state->glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    }

    /* Iterate all active entities and draw each with its mesh. */
    uint32_t capacity = entities->capacity;
    for (uint32_t i = 0; i < capacity; ++i) {
        const edit_entity_t *ent = edit_entity_store_get(entities, i);
        if (!ent) continue;
        if (ent->hidden) continue;

        /* Skip collider-only types in the solid render pass. */
        if (ent->type == EDIT_ENTITY_TYPE_COLLIDER_SPHERE ||
            ent->type == EDIT_ENTITY_TYPE_COLLIDER_BOX ||
            ent->type == EDIT_ENTITY_TYPE_COLLIDER_CAPSULE ||
            ent->type == EDIT_ENTITY_TYPE_COLLIDER_HULL) {
            continue;
        }

        /* Check for skeletal mesh — render with skinning pipeline. */
        if (ent->type == EDIT_ENTITY_TYPE_MESH &&
            i < state->entity_mesh_cache_cap &&
            state->entity_mesh_types &&
            state->entity_mesh_types[i] == VIEWPORT_MESH_SKELETAL &&
            state->skeletal_mesh_cache &&
            state->skeletal_mesh_cache[i] &&
            skel_reg) {
            /* Look up skeleton for this entity from skel_path attr. */
            uint8_t sp_t = 0, sp_s = 0;
            const void *sp_v = entity_attrs_get(&ent->attrs,
                                                SCRIPT_KEY_SKEL_PATH,
                                                &sp_t, &sp_s);
            if (sp_v && sp_t == SCRIPT_ATTR_STR) {
                const char *sp_path = (const char *)sp_v;
                if (sp_path[0] != '\0') {
                    /* Extract filename from path. */
                    const char *sp_fn = sp_path;
                    for (const char *p = sp_path; *p; p++) {
                        if (*p == '/') sp_fn = p + 1;
                    }
                    const edit_skeleton_entry_t *sk_ent =
                        edit_skeleton_registry_get(skel_reg, sp_fn);
                    if (sk_ent && sk_ent->ibms &&
                        sk_ent->skel.joint_count > 0) {
                        /* Lazy-init skinning pipeline. */
                        if (!state->skin_initialized) {
                            viewport_skinning_init(state);
                        }
                        if (state->skin_initialized) {
                            mat4_t model = build_model_matrix(ent);
                            bool selected = selection
                                ? edit_selection_contains(selection, i) : false;
                            bool is_active = (i == active_object_id);
                            const float *color = get_entity_color(ent->type);
                            /* Set selection tint on skinning shader. */
                            float tint = NO_TINT;
                            const float *tint_color = SELECT_TINT_COLOR;
                            if (is_active) {
                                tint = ACTIVE_TINT_AMOUNT;
                                tint_color = ACTIVE_TINT_COLOR;
                            } else if (selected) {
                                tint = SELECT_TINT_AMOUNT;
                            }
                            /* Check for per-entity bone pose override. */
                            edit_skeleton_entry_t sk_override;
                            const edit_skeleton_entry_t *draw_ent = sk_ent;
                            const bone_pose_block_t *bp =
                                bone_poses ? bone_pose_store_get(bone_poses, i)
                                           : NULL;
                            if (bp) {
                                sk_override = *sk_ent;
                                sk_override.skel.rest_world =
                                    (mat4_t *)bp->rest_world;
                                draw_ent = &sk_override;
                            }
                            viewport_skinning_draw(
                                state, draw_ent,
                                state->skeletal_mesh_cache[i],
                                &model, view, proj, eye_pos, color,
                                tint_color, tint, shading_mode);
                            /* Re-bind the static shader for subsequent
                             * non-skeletal entities. */
                            shader_program_bind(shader);
                            shader_uniform_set_mat4(ucache, shader,
                                "u_view", view->m, GL_FALSE);
                            shader_uniform_set_mat4(ucache, shader,
                                "u_projection", proj->m, GL_FALSE);
                            shader_uniform_set_vec3(ucache, shader,
                                "u_light_dir", LIGHT_DIR);
                            float eye2[3] = {eye_pos->x, eye_pos->y,
                                             eye_pos->z};
                            shader_uniform_set_vec3(ucache, shader,
                                "u_eye_pos", eye2);
                            continue;
                        }
                    }
                }
            }
        }

        /* Resolve the mesh for this entity. */
        const static_mesh_t *mesh;
        if (ent->type == EDIT_ENTITY_TYPE_MESH) {
            mesh = viewport_render_get_entity_mesh(state, i);
        } else {
            mesh = viewport_render_get_primitive_mesh(ent->type, state);
        }
        if (!mesh) continue;

        /* Build model matrix from entity transform. */
        mat4_t model = build_model_matrix(ent);
        shader_uniform_set_mat4(ucache, shader, "u_model", model.m, GL_FALSE);

        /* Set entity color and selection tint. */
        bool selected = selection
            ? edit_selection_contains(selection, i) : false;
        bool is_active = (i == active_object_id);
        const float *color = get_entity_color(ent->type);
        shader_uniform_set_vec3(ucache, shader, "u_color", color);

        /* Apply selection tint in the fragment shader. */
        if (is_active) {
            shader_uniform_set_vec3(ucache, shader,
                                     "u_select_color", ACTIVE_TINT_COLOR);
            shader_uniform_set_float(ucache, shader,
                                      "u_select_tint", ACTIVE_TINT_AMOUNT);
        } else if (selected) {
            shader_uniform_set_vec3(ucache, shader,
                                     "u_select_color", SELECT_TINT_COLOR);
            shader_uniform_set_float(ucache, shader,
                                      "u_select_tint", SELECT_TINT_AMOUNT);
        } else {
            shader_uniform_set_float(ucache, shader,
                                      "u_select_tint", NO_TINT);
        }

        /* Draw the mesh (all submeshes). */
        static_mesh_bind(mesh);
        for (uint32_t s = 0; s < mesh->submesh_count; ++s) {
            static_mesh_draw_submesh(mesh, s);
        }
    }
    static_mesh_unbind();

    /* Restore polygon mode after wireframe. */
    if (shading_mode == SHADING_MODE_WIREFRAME && state->glPolygonMode) {
        state->glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }
}

/**
 * @brief Render the 3D scene into a single viewport's FBO.
 *
 * Uses the viewport's camera for view/proj matrices, the shared
 * renderer for shaders/meshes, and the viewport's gizmo state.
 */
static void draw_scene_into_viewport(struct scene_editor *ed,
                                     viewport_state_t *vs) {
    viewport_render_state_t *vp = &ed->viewport;

    /* Resize FBO if needed.  Each viewport uses its own FBO sized
     * to its BSP rect (physical pixels = logical * UI scale). */
    float scale = ed->clay_be.ui_scale;
    if (scale < 1.0f) scale = 1.0f;
    int phys_w = (int)((float)vs->rect.w * scale);
    int phys_h = (int)((float)vs->rect.h * scale);
    if (phys_w <= 0 || phys_h <= 0) return;

    /* For the first viewport, reuse the shared FBO from viewport_render_state.
     * Additional viewports get their own FBOs created on demand. */
    uint32_t fbo_handle;
    int fbo_w, fbo_h;
    /* Track MSAA FBO handle for resolve blit after rendering. */
    uint32_t msaa_fbo_handle = 0;
    uint32_t resolve_fbo_handle = 0;

    if (vs == &ed->viewports[0]) {
        viewport_render_resize(vp, phys_w, phys_h);
        /* Render into MSAA FBO if available, otherwise resolve FBO. */
        fbo_handle = vp->msaa_fbo ? vp->msaa_fbo : vp->fbo;
        msaa_fbo_handle = vp->msaa_fbo;
        resolve_fbo_handle = vp->fbo;
        fbo_w = vp->fbo_width;
        fbo_h = vp->fbo_height;
        /* Keep color_tex in sync for Clay display. */
        vs->color_tex = vp->color_tex;
        vs->fbo = vp->fbo;
        vs->fbo_width = fbo_w;
        vs->fbo_height = fbo_h;
    } else {
        /* Create or resize per-viewport FBO. */
        if (!vs->fbo_valid || vs->fbo_width != phys_w ||
            vs->fbo_height != phys_h) {
            /* Delete old resources if they exist. */
            if (vs->fbo_valid) {
                vp->glDeleteFramebuffers(1, &vs->fbo);
                vp->glDeleteTextures(1, &vs->color_tex);
                vp->glDeleteRenderbuffers(1, &vs->depth_rbo);
                if (vs->msaa_fbo) {
                    vp->glDeleteFramebuffers(1, &vs->msaa_fbo);
                    vp->glDeleteRenderbuffers(1, &vs->msaa_color_rbo);
                    vp->glDeleteRenderbuffers(1, &vs->msaa_depth_rbo);
                    vs->msaa_fbo = 0;
                    vs->msaa_color_rbo = 0;
                    vs->msaa_depth_rbo = 0;
                }
            }

            /* ---- Resolve FBO (single-sample, texture for display) ---- */
            vp->glGenFramebuffers(1, &vs->fbo);
            vp->glGenTextures(1, &vs->color_tex);
            vp->glGenRenderbuffers(1, &vs->depth_rbo);

            vp->glBindTexture(GL_TEXTURE_2D, vs->color_tex);
            vp->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                             phys_w, phys_h, 0,
                             GL_RGBA, GL_UNSIGNED_BYTE, NULL);
            vp->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                                GL_LINEAR);
            vp->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                                GL_LINEAR);
            vp->glBindTexture(GL_TEXTURE_2D, 0);

            vp->glBindRenderbuffer(GL_RENDERBUFFER, vs->depth_rbo);
            vp->glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8,
                                      phys_w, phys_h);
            vp->glBindRenderbuffer(GL_RENDERBUFFER, 0);

            vp->glBindFramebuffer(GL_FRAMEBUFFER, vs->fbo);
            vp->glFramebufferTexture2D(GL_FRAMEBUFFER,
                                       GL_COLOR_ATTACHMENT0,
                                       GL_TEXTURE_2D, vs->color_tex, 0);
            vp->glFramebufferRenderbuffer(GL_FRAMEBUFFER,
                                          GL_DEPTH_STENCIL_ATTACHMENT,
                                          GL_RENDERBUFFER, vs->depth_rbo);
            vp->glBindFramebuffer(GL_FRAMEBUFFER, 0);

            /* ---- MSAA FBO (multisample renderbuffers) ---- */
            if (vp->msaa_samples > 1 &&
                vp->glRenderbufferStorageMultisample) {
                int s = vp->msaa_samples;

                vp->glGenRenderbuffers(1, &vs->msaa_color_rbo);
                vp->glBindRenderbuffer(GL_RENDERBUFFER, vs->msaa_color_rbo);
                vp->glRenderbufferStorageMultisample(GL_RENDERBUFFER, s,
                                                     GL_RGBA8,
                                                     phys_w, phys_h);

                vp->glGenRenderbuffers(1, &vs->msaa_depth_rbo);
                vp->glBindRenderbuffer(GL_RENDERBUFFER, vs->msaa_depth_rbo);
                vp->glRenderbufferStorageMultisample(GL_RENDERBUFFER, s,
                                                     GL_DEPTH24_STENCIL8,
                                                     phys_w, phys_h);

                vp->glGenFramebuffers(1, &vs->msaa_fbo);
                vp->glBindFramebuffer(GL_FRAMEBUFFER, vs->msaa_fbo);
                vp->glFramebufferRenderbuffer(GL_FRAMEBUFFER,
                                              GL_COLOR_ATTACHMENT0,
                                              GL_RENDERBUFFER,
                                              vs->msaa_color_rbo);
                vp->glFramebufferRenderbuffer(GL_FRAMEBUFFER,
                                              GL_DEPTH_STENCIL_ATTACHMENT,
                                              GL_RENDERBUFFER,
                                              vs->msaa_depth_rbo);
                vp->glBindFramebuffer(GL_FRAMEBUFFER, 0);
            }

            vs->fbo_width = phys_w;
            vs->fbo_height = phys_h;
            vs->fbo_valid = true;
        }
        /* Render into MSAA FBO if available. */
        fbo_handle = vs->msaa_fbo ? vs->msaa_fbo : vs->fbo;
        msaa_fbo_handle = vs->msaa_fbo;
        resolve_fbo_handle = vs->fbo;
        fbo_w = vs->fbo_width;
        fbo_h = vs->fbo_height;
    }

    /* Compute camera matrices from this viewport's camera. */
    float aspect = (fbo_w > 0 && fbo_h > 0)
        ? (float)fbo_w / (float)fbo_h : 1.0f;
    mat4_t view, proj;
    editor_camera_view_matrix(&vs->camera, &view);
    editor_camera_projection_matrix(&vs->camera, aspect, &proj);
    vec3_t eye_pos = editor_camera_eye_position(&vs->camera);

    /* Bind this viewport's FBO. */
    vp->glBindFramebuffer(GL_FRAMEBUFFER, fbo_handle);
    vp->glViewport(0, 0, fbo_w, fbo_h);
    vp->glClearColor(0.12f, 0.12f, 0.14f, 1.0f);
    vp->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT
                | GL_STENCIL_BUFFER_BIT);
    vp->glEnable(GL_DEPTH_TEST);
    vp->glEnable(GL_CULL_FACE);
    if (msaa_fbo_handle) {
        vp->glEnable(GL_MULTISAMPLE);
    }

    /* Draw grid. */
    viewport_render_draw_grid(vp, &view, &proj);

    /* Draw all entities (shared across viewports). */
    viewport_render_draw_entities(vp, &ed->entities, &ed->selection,
                                   ed->active_object_id,
                                   &view, &proj, &eye_pos,
                                   vs->shading_mode,
                                   &ed->skeleton_registry,
                                   &ed->bone_poses);

    /* Draw collision wireframe overlay (if toggled on). */
    if (vs->show_collision_wireframe) {
        viewport_render_draw_collision_overlay(vp, &ed->entities,
                                                &view, &proj);
    }

    /* Draw bone overlay for selected entities with skeletons. */
    {
        uint32_t ent_cap = ed->entities.capacity;
        for (uint32_t ei = 0; ei < ent_cap; ei++) {
            if (!edit_selection_contains(&ed->selection, ei)) continue;
            const edit_entity_t *bent = edit_entity_store_get(&ed->entities, ei);
            if (!bent || !bent->active || bent->hidden) continue;

            /* Look up skel_path attr. */
            uint8_t at = 0, as = 0;
            const void *sp = entity_attrs_get(&bent->attrs,
                                               SCRIPT_KEY_SKEL_PATH, &at, &as);
            if (!sp || at != SCRIPT_ATTR_STR) continue;
            const char *spath = (const char *)sp;
            if (spath[0] == '\0') continue;

            /* Extract filename from full path for registry lookup. */
            const char *fname = spath;
            for (const char *p = spath; *p; p++) {
                if (*p == '/') fname = p + 1;
            }
            const edit_skeleton_entry_t *entry =
                edit_skeleton_registry_get(&ed->skeleton_registry, fname);
            if (!entry) continue;

            mat4_t bmodel = build_model_matrix(bent);
            /* Set view/proj uniforms for the flat shader. */
            shader_uniform_set_mat4(&vp->flat_uniforms, &vp->flat_shader,
                                     "u_view", view.m, GL_FALSE);
            shader_uniform_set_mat4(&vp->flat_uniforms, &vp->flat_shader,
                                     "u_projection", proj.m, GL_FALSE);

            /* Use per-entity pose override if available. */
            skeleton_def_t bone_skel_view;
            const skeleton_def_t *draw_skel = &entry->skel;
            const bone_pose_block_t *bone_bp =
                bone_pose_store_get(&ed->bone_poses, ei);
            if (bone_bp) {
                bone_skel_view = entry->skel;
                bone_skel_view.rest_world = (mat4_t *)bone_bp->rest_world;
                bone_skel_view.tail_positions =
                    (float *)bone_bp->tail_positions;
                draw_skel = &bone_skel_view;
            }
            viewport_render_draw_bone_overlay(
                vp, draw_skel, ei, bmodel.m, &ed->bone_selection);
        }
    }

    /* Skeleton mode: load and render ghost preview mesh. */
    if (ed->skeleton_mode.active &&
        ed->skeleton_mode.preview_path[0] != '\0') {
        /* Lazy-load the preview mesh directly into the mesh registry. */
        if (!ed->skeleton_mode.preview_loaded) {
            char prev_full[512];
            snprintf(prev_full, sizeof(prev_full), "%s/%s",
                     ed->config.asset_dir, ed->skeleton_mode.preview_path);
            FILE *pf = fopen(prev_full, "rb");
            if (pf) {
                fseek(pf, 0, SEEK_END);
                long psz = ftell(pf);
                fseek(pf, 0, SEEK_SET);
                if (psz > 0) {
                    uint8_t *pdata = (uint8_t *)malloc((size_t)psz);
                    if (pdata && fread(pdata, 1, (size_t)psz, pf) == (size_t)psz) {
                        mesh_slot_t slot;
                        memset(&slot, 0, sizeof(slot));
                        if (mesh_vao_deserialize(pdata, (size_t)psz, &slot)) {
                            static_mesh_create_info_t ci;
                            memset(&ci, 0, sizeof(ci));
                            ci.positions    = slot.positions;
                            ci.normals      = slot.normals;
                            ci.uv0          = slot.uvs[0];
                            ci.indices      = slot.indices;
                            ci.vertex_count = slot.vertex_count;
                            ci.index_count  = slot.index_count;
                            mesh_handle_t mh;
                            if (mesh_registry_insert_static(
                                    &vp->meshes, &ci, &mh) == 0) {
                                ed->skeleton_mode.preview_mesh_index = mh.index;
                                ed->skeleton_mode.preview_mesh_gen = mh.generation;
                            }
                            /* slot data consumed by registry insert. */
                        }
                    }
                    free(pdata);
                }
                fclose(pf);
            }
            ed->skeleton_mode.preview_loaded = true;
        }

        const static_mesh_t *preview_mesh = NULL;
        if (ed->skeleton_mode.preview_mesh_gen != 0) {
            mesh_handle_t ph = {
                ed->skeleton_mode.preview_mesh_index,
                ed->skeleton_mode.preview_mesh_gen
            };
            preview_mesh = mesh_registry_get_static(&vp->meshes, ph);
        }
        if (preview_mesh) {
            /* Render as ghost wireframe using flat shader. */
            shader_program_bind(&vp->flat_shader);
            shader_uniform_set_mat4(&vp->flat_uniforms,
                &vp->flat_shader, "u_view", view.m, GL_FALSE);
            shader_uniform_set_mat4(&vp->flat_uniforms,
                &vp->flat_shader, "u_projection", proj.m, GL_FALSE);
            mat4_t identity = mat4_identity();
            shader_uniform_set_mat4(&vp->flat_uniforms,
                &vp->flat_shader, "u_model", identity.m, GL_FALSE);
            float ghost_color[3] = {0.3f, 0.35f, 0.4f};
            shader_uniform_set_vec3(&vp->flat_uniforms,
                &vp->flat_shader, "u_color", ghost_color);

            vp->glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
            vp->glDisable(GL_CULL_FACE);

            static_mesh_bind(preview_mesh);
            for (uint32_t s = 0; s < preview_mesh->submesh_count; s++) {
                static_mesh_draw_submesh(preview_mesh, s);
            }
            static_mesh_unbind();

            vp->glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            vp->glEnable(GL_CULL_FACE);
        }
    }

    /* Skeleton mode: draw bones directly from registry.
     * Always draw when skeleton mode is active, even if no entities. */
    if (ed->skeleton_mode.active && ed->skeleton_mode.skel_path[0] != '\0') {
        const edit_skeleton_entry_t *sk_entry =
            edit_skeleton_registry_get(&ed->skeleton_registry,
                                        ed->skeleton_mode.skel_path);
        if (sk_entry && sk_entry->skel.joint_count > 0 &&
            sk_entry->skel.tail_positions) {
            /* Bind flat shader before setting uniforms — in skeleton mode
             * from an asset, no entity draw has bound it yet. */
            shader_program_bind(&vp->flat_shader);
            shader_uniform_set_mat4(&vp->flat_uniforms, &vp->flat_shader,
                                     "u_view", view.m, GL_FALSE);
            shader_uniform_set_mat4(&vp->flat_uniforms, &vp->flat_shader,
                                     "u_projection", proj.m, GL_FALSE);
            mat4_t skel_model = mat4_identity();
            uint32_t skel_eid = ed->skeleton_mode.entity_id;
            if (skel_eid != UINT32_MAX) {
                const edit_entity_t *skel_ent =
                    edit_entity_store_get(&ed->entities, skel_eid);
                if (skel_ent) skel_model = build_model_matrix(skel_ent);
            }
            viewport_render_draw_bone_overlay(
                vp, &sk_entry->skel, skel_eid, skel_model.m,
                &ed->bone_selection);
        }
    }

    /* Draw prefab hull wireframes in prefab mode. */
    if (ed->prefab_mode.active) {
        static prefab_hull_cache_t s_hull_cache;
        static bool s_hull_cache_init = false;
        if (!s_hull_cache_init) {
            prefab_hull_cache_init(&s_hull_cache);
            s_hull_cache_init = true;
        }
        /* Find skeleton for root entity to get bone count. */
        uint32_t root_id = ed->prefab_mode.root_entity_id;
        const edit_entity_t *root_ent =
            edit_entity_store_get(&ed->entities, root_id);
        if (root_ent) {
            uint8_t hst = 0, hss = 0;
            const void *hsp = entity_attrs_get(&root_ent->attrs,
                                                SCRIPT_KEY_SKEL_PATH,
                                                &hst, &hss);
            if (hsp && hst == SCRIPT_ATTR_STR) {
                const char *hspath = (const char *)hsp;
                const char *hfname = hspath;
                for (const char *p = hspath; *p; p++) {
                    if (*p == '/') hfname = p + 1;
                }
                const edit_skeleton_entry_t *hentry =
                    edit_skeleton_registry_get(&ed->skeleton_registry, hfname);
                if (hentry) {
                    /* Only rebuild hulls when prefab is marked dirty
                     * (entity added/moved/deleted) — not every frame.
                     * Use a local generation to avoid clearing the shared
                     * dirty flag (outliner also reads it). */
                    static uint32_t s_hull_gen = 0;
                    if (ed->prefab_mode.dirty_gen != s_hull_gen ||
                        s_hull_cache.count == 0) {
                        prefab_hull_cache_rebuild(&s_hull_cache, &ed->entities,
                                                  root_id,
                                                  hentry->skel.joint_count);
                        s_hull_gen = ed->prefab_mode.dirty_gen;
                    }
                    prefab_hull_overlay_draw(vp, &s_hull_cache,
                                             &view, &proj);
                }
            }
        }
    }

    /* Update gizmo position and orientation for this viewport.
     * Skip position update in per-object mode during drag — the
     * picked entity's position was set at drag start and must remain
     * stable for delta computation. */
    bool has_bones = edit_bone_selection_count(&ed->bone_selection) > 0;
    if ((edit_selection_count(&ed->selection) > 0 || has_bones) &&
        !(vs->per_object_gizmo && vs->gizmo.dragging)) {
        if (vs->gizmo.basis == TRANSFORM_BASIS_CURSOR) {
            vs->gizmo.position = vs->cursor_3d;
        } else {
            vec3_t center = {0, 0, 0};
            uint32_t sel_n = 0;
            for (uint32_t i = 0; i < ed->entities.capacity; ++i) {
                if (!edit_selection_contains(&ed->selection, i)) continue;
                const edit_entity_t *ent =
                    edit_entity_store_get(&ed->entities, i);
                if (!ent) continue;
                if (ent->hidden) continue;
                center.x += ent->pos[0];
                center.y += ent->pos[1];
                center.z += ent->pos[2];
                sel_n++;
            }
            if (sel_n > 0) {
                float inv = 1.0f / (float)sel_n;
                center.x *= inv;
                center.y *= inv;
                center.z *= inv;
            }
            vs->gizmo.position = center;
        }

        /* Override gizmo position to bone centroid if bones are selected. */
        uint32_t bone_eid = ed->bone_selection.entity_id;
        if (edit_bone_selection_count(&ed->bone_selection) > 0 &&
            bone_eid != EDIT_BONE_SEL_NONE) {
            const edit_entity_t *bge = edit_entity_store_get(
                &ed->entities, bone_eid);
            if (bge && bge->active) {
                uint8_t bgat = 0, bgas = 0;
                const void *bgsp = entity_attrs_get(
                    &bge->attrs, SCRIPT_KEY_SKEL_PATH, &bgat, &bgas);
                if (bgsp && bgat == SCRIPT_ATTR_STR) {
                    const char *bgp = (const char *)bgsp;
                    if (bgp[0] != '\0') {
                        const char *bgfn = bgp;
                        for (const char *p = bgp; *p; p++) {
                            if (*p == '/') bgfn = p + 1;
                        }
                        const edit_skeleton_entry_t *bge2 =
                            edit_skeleton_registry_get(
                                &ed->skeleton_registry, bgfn);
                        if (bge2 && bge2->skel.joint_count > 0) {
                            uint32_t bn = 0;
                            const uint32_t *bbones =
                                edit_bone_selection_bones(
                                    &ed->bone_selection, &bn);
                            if (bbones && bn > 0) {
                                vec3_t bcent = {0, 0, 0};
                                uint32_t bvalid = 0;
                                for (uint32_t bi = 0; bi < bn; bi++) {
                                    uint32_t idx = bbones[bi];
                                    if (idx >= bge2->skel.joint_count) continue;
                                    bcent.x += bge2->skel.rest_world[idx].m[12]
                                             + bge->pos[0];
                                    bcent.y += bge2->skel.rest_world[idx].m[13]
                                             + bge->pos[1];
                                    bcent.z += bge2->skel.rest_world[idx].m[14]
                                             + bge->pos[2];
                                    bvalid++;
                                }
                                if (bvalid > 0) {
                                    float binv = 1.0f / (float)bvalid;
                                    bcent.x *= binv;
                                    bcent.y *= binv;
                                    bcent.z *= binv;
                                    vs->gizmo.position = bcent;
                                }
                            }
                        }
                    }
                }
            }
        }

        /* Compute gizmo orientation from basis mode.
         * Scale always operates in local space, so force local
         * orientation when the scale gizmo is active. */
        const quat_t *active_orient = NULL;
        transform_basis_t effective_basis = vs->gizmo.basis;
        if (vs->gizmo.mode == GIZMO_MODE_SCALE) {
            effective_basis = TRANSFORM_BASIS_LOCAL;
        }
        if (effective_basis == TRANSFORM_BASIS_LOCAL &&
            ed->active_object_id != EDIT_ENTITY_INVALID_ID) {
            const edit_entity_t *active_ent =
                edit_entity_store_get(&ed->entities, ed->active_object_id);
            if (active_ent) {
                active_orient = &active_ent->orientation;
            }
        }
        vs->gizmo.orientation = transform_basis_orientation(
            effective_basis, active_orient, &view);

        /* Update rotation arc quadrant selection based on camera. */
        gizmo_update_arc_quadrants(&vs->gizmo, eye_pos);
    }

    /* Draw transform gizmo (only in focused viewport). */
    bool is_focused = (vs == &ed->viewports[ed->vp_bsp.focused_viewport]);
    if (is_focused) {
        uint32_t bg_eid = ed->bone_selection.entity_id;
        bool bones_active = edit_bone_selection_count(&ed->bone_selection) > 0
                            && bg_eid != EDIT_BONE_SEL_NONE;

        if (bones_active) {
            /* When bones are selected, draw bone gizmos — suppress
             * entity gizmos entirely so transforms target bones.
             * per_object_gizmo: one gizmo per bone; otherwise: single
             * gizmo at the centroid of selected bones. */
            const edit_entity_t *bg_ent = edit_entity_store_get(
                &ed->entities, bg_eid);
            if (bg_ent && bg_ent->active && !bg_ent->hidden) {
                uint8_t bg_at = 0, bg_as = 0;
                const void *bg_sp = entity_attrs_get(
                    &bg_ent->attrs, SCRIPT_KEY_SKEL_PATH,
                    &bg_at, &bg_as);
                if (bg_sp && bg_at == SCRIPT_ATTR_STR) {
                    const char *bg_path = (const char *)bg_sp;
                    if (bg_path[0] != '\0') {
                        const char *bg_fn = bg_path;
                        for (const char *p = bg_path; *p; p++) {
                            if (*p == '/') bg_fn = p + 1;
                        }
                        const edit_skeleton_entry_t *bg_entry =
                            edit_skeleton_registry_get(
                                &ed->skeleton_registry, bg_fn);
                        if (bg_entry) {
                            /* Use per-entity pose override for gizmo positions. */
                            skeleton_def_t gizmo_skel_view;
                            const skeleton_def_t *gizmo_skel = &bg_entry->skel;
                            const bone_pose_block_t *gizmo_bp =
                                bone_pose_store_get(&ed->bone_poses, bg_eid);
                            if (gizmo_bp) {
                                gizmo_skel_view = bg_entry->skel;
                                gizmo_skel_view.rest_world =
                                    (mat4_t *)gizmo_bp->rest_world;
                                gizmo_skel_view.tail_positions =
                                    (float *)gizmo_bp->tail_positions;
                                gizmo_skel = &gizmo_skel_view;
                            }
                            mat4_t bg_model = build_model_matrix(bg_ent);
                            per_bone_gizmo_t bone_gizmos[EDIT_BONE_SEL_MAX];
                            uint32_t bg_count = per_bone_gizmo_build(
                                gizmo_skel,
                                &ed->bone_selection,
                                &bg_model,
                                vs->gizmo.mode,
                                vs->gizmo.basis,
                                bone_gizmos,
                                EDIT_BONE_SEL_MAX);
                            if (bg_count > 0) {
                                if (vs->per_object_gizmo) {
                                    scene_gizmo_bone_draw(
                                        vp, bone_gizmos, bg_count,
                                        &view, &proj, &eye_pos,
                                        &ed->selection);
                                } else {
                                    /* Single gizmo at centroid of selected bones. */
                                    vec3_t centroid = {0, 0, 0};
                                    for (uint32_t gi = 0; gi < bg_count; gi++) {
                                        centroid.x += bone_gizmos[gi].gizmo.position.x;
                                        centroid.y += bone_gizmos[gi].gizmo.position.y;
                                        centroid.z += bone_gizmos[gi].gizmo.position.z;
                                    }
                                    centroid.x /= (float)bg_count;
                                    centroid.y /= (float)bg_count;
                                    centroid.z /= (float)bg_count;
                                    vs->gizmo.position = centroid;
                                    viewport_render_draw_gizmo(
                                        vp, &vs->gizmo, &ed->selection,
                                        &view, &proj);
                                }
                            }
                        }
                    }
                }
            }
        } else if (vs->per_object_gizmo) {
            /* Per-object mode: one gizmo per selected entity. */
            scene_gizmo_per_object_draw(vp, &ed->entities, &ed->selection,
                                         vs->gizmo.mode, vs->gizmo.basis,
                                         &view, &proj, &eye_pos);
        } else {
            viewport_render_draw_gizmo(vp, &vs->gizmo, &ed->selection,
                                       &view, &proj);
        }
    }

    /* Draw 3D cursor crosshair. */
    viewport_render_draw_cursor(vp, &vs->cursor_3d, &view, &proj);

    /* Draw box select rectangle if active in this viewport. */
    if (vs->box_selecting) {
        float dsc = ed->clay_be.ui_scale;
        if (dsc < 1.0f) dsc = 1.0f;
        float cur_lx = ed->ui.mouse_x / dsc;
        float cur_ly = ed->ui.mouse_y / dsc;
        float bx0 = (vs->box_select_start_x - (float)vs->rect.x)
                     / (float)vs->rect.w;
        float by0 = (vs->box_select_start_y - (float)vs->rect.y)
                     / (float)vs->rect.h;
        float bx1 = (cur_lx - (float)vs->rect.x) / (float)vs->rect.w;
        float by1 = (cur_ly - (float)vs->rect.y) / (float)vs->rect.h;
        viewport_render_draw_box_select(vp, bx0, by0, bx1, by1);
    }

    /* Resolve MSAA → single-sample FBO for display. */
    if (msaa_fbo_handle && resolve_fbo_handle && vp->glBlitFramebuffer) {
        vp->glBindFramebuffer(GL_READ_FRAMEBUFFER, msaa_fbo_handle);
        vp->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolve_fbo_handle);
        vp->glBlitFramebuffer(0, 0, fbo_w, fbo_h,
                               0, 0, fbo_w, fbo_h,
                               GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }

    /* Unbind FBO. */
    vp->glDisable(GL_CULL_FACE);
    vp->glDisable(GL_DEPTH_TEST);
    vp->glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void viewport_render_draw_scene(struct scene_editor *ed) {
    if (!ed) return;
    viewport_render_state_t *vp = &ed->viewport;
    if (!vp->initialized) return;

    /* Compute BSP rects for all viewports. */
    panel_rect_t panel = panel_layout_get_rect(&ed->layout, PANEL_VIEWPORT);
    panel_rect_t vp_rects[VIEWPORT_MAX_COUNT];
    memset(vp_rects, 0, sizeof(vp_rects));
    viewport_bsp_compute_rects(&ed->vp_bsp, &panel, vp_rects);

    /* Store computed rects into viewport states. */
    for (int i = 0; i < VIEWPORT_MAX_COUNT; i++) {
        ed->viewports[i].rect = vp_rects[i];
    }

    /* Render 3D scene into each active viewport's FBO. */
    for (int i = 0; i < VIEWPORT_MAX_COUNT; i++) {
        if (!ed->viewports[i].active) continue;
        if (ed->viewports[i].rect.w <= 0 || ed->viewports[i].rect.h <= 0)
            continue;
        draw_scene_into_viewport(ed, &ed->viewports[i]);
    }
}

/* viewport_render_destroy_primitives removed — primitives are now
 * owned by the mesh registry and destroyed via mesh_registry_destroy(). */
