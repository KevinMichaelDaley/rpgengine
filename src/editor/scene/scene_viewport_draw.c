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

#include "ferrum/renderer/gl_constants.h"
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
        handle = state->mesh_box;
        break;
    case EDIT_ENTITY_TYPE_SPHERE:
        handle = state->mesh_sphere;
        break;
    case EDIT_ENTITY_TYPE_CAPSULE:
        handle = state->mesh_capsule;
        break;
    case EDIT_ENTITY_TYPE_HALFSPACE:
        handle = state->mesh_plane;
        break;
    case EDIT_ENTITY_TYPE_MESH:
        return NULL;
    case EDIT_ENTITY_TYPE_MARKER:
        handle = state->mesh_sphere;
        break;
    default:
        handle = state->mesh_box;
        break;
    }

    return mesh_registry_get_static(&state->meshes, handle);
}

/**
 * @brief Get the color for an entity based on its type and selection state.
 */
static const float *get_entity_color(uint32_t entity_type, bool selected,
                                       bool is_active) {
    /* Selection/active state is indicated by the outline overlay pass,
     * not by changing the entity's base color. */
    (void)selected;
    (void)is_active;

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

/**
 * @brief Build a model matrix from entity position, orientation, and scale.
 *
 * Uses the quaternion orientation (authoritative) to build the rotation
 * matrix, avoiding euler angle order-dependence issues.
 */
static mat4_t build_model_matrix(const edit_entity_t *ent) {
    mat4_t scale = mat4_scaling(ent->scale[0], ent->scale[1], ent->scale[2]);
    mat4_t rot;
    quat_to_mat4(ent->orientation, &rot);
    mat4_t trans = mat4_translation(ent->pos[0], ent->pos[1], ent->pos[2]);

    /* T * R * S */
    return mat4_mul(trans, mat4_mul(rot, scale));
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
                                    shading_mode_t shading_mode) {
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

    /* Wireframe: set polygon mode to lines. */
    if (shading_mode == SHADING_MODE_WIREFRAME && state->glPolygonMode) {
        state->glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    }

    /* Iterate all active entities and draw each with its mesh. */
    uint32_t capacity = entities->capacity;
    for (uint32_t i = 0; i < capacity; ++i) {
        const edit_entity_t *ent = edit_entity_store_get(entities, i);
        if (!ent) continue;

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

        /* Set entity color (selection-aware, active object highlight). */
        bool selected = selection
            ? edit_selection_contains(selection, i) : false;
        bool is_active = (i == active_object_id);
        const float *color = get_entity_color(ent->type, selected, is_active);
        shader_uniform_set_vec3(ucache, shader, "u_color", color);

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
    if (vs == &ed->viewports[0]) {
        viewport_render_resize(vp, phys_w, phys_h);
        fbo_handle = vp->fbo;
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
            }
            /* Create FBO. */
            vp->glGenFramebuffers(1, &vs->fbo);
            vp->glGenTextures(1, &vs->color_tex);
            vp->glGenRenderbuffers(1, &vs->depth_rbo);

            /* Set up color texture. */
            vp->glBindTexture(GL_TEXTURE_2D, vs->color_tex);
            vp->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                             phys_w, phys_h, 0,
                             GL_RGBA, GL_UNSIGNED_BYTE, NULL);
            vp->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                                GL_LINEAR);
            vp->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                                GL_LINEAR);
            vp->glBindTexture(GL_TEXTURE_2D, 0);

            /* Set up depth+stencil renderbuffer. */
            vp->glBindRenderbuffer(GL_RENDERBUFFER, vs->depth_rbo);
            vp->glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8,
                                      phys_w, phys_h);
            vp->glBindRenderbuffer(GL_RENDERBUFFER, 0);

            /* Attach to FBO. */
            vp->glBindFramebuffer(GL_FRAMEBUFFER, vs->fbo);
            vp->glFramebufferTexture2D(GL_FRAMEBUFFER,
                                       GL_COLOR_ATTACHMENT0,
                                       GL_TEXTURE_2D, vs->color_tex, 0);
            vp->glFramebufferRenderbuffer(GL_FRAMEBUFFER,
                                          GL_DEPTH_STENCIL_ATTACHMENT,
                                          GL_RENDERBUFFER, vs->depth_rbo);
            vp->glBindFramebuffer(GL_FRAMEBUFFER, 0);

            vs->fbo_width = phys_w;
            vs->fbo_height = phys_h;
            vs->fbo_valid = true;
        }
        fbo_handle = vs->fbo;
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

    /* Draw grid. */
    viewport_render_draw_grid(vp, &view, &proj);

    /* Draw all entities (shared across viewports). */
    viewport_render_draw_entities(vp, &ed->entities, &ed->selection,
                                   ed->active_object_id,
                                   &view, &proj, &eye_pos,
                                   vs->shading_mode);

    /* Draw selection outlines. */
    viewport_render_draw_selection_outline(vp, &ed->entities, &ed->selection,
                                            ed->active_object_id, &view, &proj,
                                            &eye_pos, vs->camera.fov, fbo_h,
                                            vs->shading_mode);

    /* Update gizmo position and orientation for this viewport. */
    if (edit_selection_count(&ed->selection) > 0) {
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

        /* Compute gizmo orientation from basis mode. */
        const quat_t *active_orient = NULL;
        if (vs->gizmo.basis == TRANSFORM_BASIS_LOCAL &&
            ed->active_object_id != EDIT_ENTITY_INVALID_ID) {
            const edit_entity_t *active_ent =
                edit_entity_store_get(&ed->entities, ed->active_object_id);
            if (active_ent) {
                active_orient = &active_ent->orientation;
            }
        }
        vs->gizmo.orientation = transform_basis_orientation(
            vs->gizmo.basis, active_orient, &view);

        /* Update rotation arc quadrant selection based on camera. */
        gizmo_update_arc_quadrants(&vs->gizmo, eye_pos);
    }

    /* Draw transform gizmo (only in focused viewport). */
    bool is_focused = (vs == &ed->viewports[ed->vp_bsp.focused_viewport]);
    if (is_focused) {
        viewport_render_draw_gizmo(vp, &vs->gizmo, &ed->selection,
                                   &view, &proj);
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
