/**
 * @file scene_viewport_overlay.c
 * @brief Viewport overlay rendering: 3D cursor, selection outline, box select.
 *
 * Draws overlay geometry after the main entity pass. The 3D cursor
 * is rendered without depth test (always visible). Selection outlines
 * are drawn as slightly scaled-up solid meshes behind the selected
 * entities to create a visible border effect. Box select rectangle
 * is drawn as an NDC-space line rectangle during drag.
 *
 * Non-static functions (3 / 4 limit):
 *   viewport_render_draw_cursor
 *   viewport_render_draw_selection_outline
 *   viewport_render_draw_box_select
 */

#include "ferrum/editor/scene/scene_viewport_render.h"
#include "ferrum/editor/scene/scene_main.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_entity_pivot.h"
#include "ferrum/editor/edit_selection.h"
#include "ferrum/editor/viewport/viewport_shading.h"

#include "ferrum/renderer/gl_constants.h"
#include "ferrum/math/mat4.h"
#include "ferrum/math/quat.h"
#include "ferrum/math/vec3.h"

#include <math.h>

/* ---- Constants ---- */

/** 3D cursor arm length (world units in each direction). */
#define CURSOR_ARM_LENGTH 0.5f

/** Selection outline color (orange). */
static const float OUTLINE_COLOR[3] = {1.0f, 0.5f, 0.0f};

/** Active object outline color (whitish-yellow). */
static const float OUTLINE_ACTIVE_COLOR[3] = {1.0f, 0.9f, 0.5f};

/** Minimum selection outline thickness in pixels.  The outline scale
 *  is computed per-entity so distant objects still get a visible border. */
#define OUTLINE_MIN_PX 1.5f

/** Maximum outline scale (prevents absurdly thick outlines on tiny
 *  objects very close to camera). */
#define OUTLINE_MAX_SCALE 1.05f

/* ---- 3D Cursor ---- */

void viewport_render_draw_cursor(viewport_render_state_t *state,
                                   const vec3_t *pos,
                                   const mat4_t *view,
                                   const mat4_t *proj) {
    if (!state || !state->initialized || !pos) return;

    /* 6 vertices: 3 lines x 2 endpoints.
     * Each vertex: x, y, z, r, g, b (6 floats) — matches grid format. */
    float cx = pos->x, cy = pos->y, cz = pos->z;
    float L = CURSOR_ARM_LENGTH;

    float verts[6 * 6] = {
        /* X axis (red) */
        cx - L, cy, cz,  1.0f, 0.2f, 0.2f,
        cx + L, cy, cz,  1.0f, 0.2f, 0.2f,
        /* Y axis (green) */
        cx, cy - L, cz,  0.2f, 1.0f, 0.2f,
        cx, cy + L, cz,  0.2f, 1.0f, 0.2f,
        /* Z axis (blue) */
        cx, cy, cz - L,  0.3f, 0.3f, 1.0f,
        cx, cy, cz + L,  0.3f, 0.3f, 1.0f,
    };

    mat4_t vp = mat4_mul(*proj, *view);

    shader_program_bind(&state->grid_shader);
    shader_uniform_set_mat4(&state->grid_uniforms, &state->grid_shader,
                             "u_vp", vp.m, GL_FALSE);

    /* Upload cursor vertices to overlay VBO (separate from grid). */
    state->overlay_vao.glBindVertexArray(state->overlay_vao.handle);
    vbo_upload(&state->overlay_vbo, GL_ARRAY_BUFFER, verts, sizeof(verts),
               GL_DYNAMIC_DRAW);

    vao_attribute_t attrs[2] = {
        {0, 3, GL_FLOAT, GL_FALSE, 0,                 0},
        {1, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), 0},
    };
    vao_bind_attributes(&state->overlay_vao, &state->overlay_vbo, attrs, 2,
                        6 * sizeof(float));

    /* Draw without depth test so cursor is always visible. */
    state->glDisable(GL_DEPTH_TEST);
    state->glDrawArrays(GL_LINES, 0, 6);
    state->glEnable(GL_DEPTH_TEST);

    state->overlay_vao.glBindVertexArray(0);
}

/* ---- Selection Outline ---- */

/**
 * @brief Build a model matrix from entity transform with scale factor.
 *
 * Uses quaternion orientation for rotation.
 * Includes pivot offset: T(pos) * R * S(scale*extra) * T(-pivot_offset).
 */
static mat4_t build_outline_model(const edit_entity_t *ent, float extra_scale) {
    mat4_t pivot_shift = mat4_translation(
        -ent->pivot_offset[0], -ent->pivot_offset[1],
        -ent->pivot_offset[2]);
    mat4_t scale = mat4_scaling(
        ent->scale[0] * extra_scale,
        ent->scale[1] * extra_scale,
        ent->scale[2] * extra_scale);
    mat4_t rot;
    quat_to_mat4(ent->orientation, &rot);
    mat4_t trans = mat4_translation(ent->pos[0], ent->pos[1], ent->pos[2]);
    return mat4_mul(trans, mat4_mul(rot, mat4_mul(scale, pivot_shift)));
}

/**
 * @brief Compute an outline scale factor that guarantees at least
 *        OUTLINE_MIN_PX pixels of visible border on screen.
 *
 * Uses the camera distance, FOV, and FBO height to determine how many
 * world units correspond to one pixel at the entity's depth.  The entity's
 * average scale determines its approximate radius so we can convert
 * the minimum pixel offset into a multiplicative scale factor.
 *
 * @param ent         Entity (for position and scale).
 * @param eye_pos     Camera eye position.
 * @param fov_y       Vertical FOV in radians.
 * @param fbo_height  FBO height in pixels.
 * @return Scale factor >= 1.0.
 */
static float compute_outline_scale_(const edit_entity_t *ent,
                                     const vec3_t *eye_pos,
                                     float fov_y, int fbo_height) {
    /* Distance from camera to entity geometry center. */
    float gc[3];
    edit_entity_geometry_center(ent, gc);
    float dx = gc[0] - eye_pos->x;
    float dy = gc[1] - eye_pos->y;
    float dz = gc[2] - eye_pos->z;
    float dist = sqrtf(dx * dx + dy * dy + dz * dz);
    if (dist < 0.01f) dist = 0.01f;

    /* World units per pixel at this distance:
     *   visible_height = 2 * dist * tan(fov/2)
     *   world_per_px   = visible_height / fbo_height  */
    float half_tan = tanf(fov_y * 0.5f);
    float world_per_px = (2.0f * dist * half_tan) / (float)fbo_height;

    /* Minimum world-space offset for OUTLINE_MIN_PX pixels. */
    float min_offset = OUTLINE_MIN_PX * world_per_px;

    /* Approximate entity radius from average scale. */
    float avg_scale = (ent->scale[0] + ent->scale[1] + ent->scale[2]) / 3.0f;
    if (avg_scale < 0.001f) avg_scale = 0.001f;
    float radius = avg_scale * 0.5f;

    /* Scale factor = 1 + offset/radius, clamped. */
    float scale = 1.0f + min_offset / radius;
    if (scale > OUTLINE_MAX_SCALE) scale = OUTLINE_MAX_SCALE;
    return scale;
}

/** Wireframe selection outline color (yellow). */
static const float OUTLINE_WIRE_COLOR[3] = {1.0f, 0.9f, 0.2f};

/** Wireframe active object outline color (bright white-yellow). */
static const float OUTLINE_WIRE_ACTIVE_COLOR[3] = {1.0f, 1.0f, 0.6f};

/** Wireframe selection line width. */
#define OUTLINE_WIRE_WIDTH 2.5f

void viewport_render_draw_selection_outline(viewport_render_state_t *state,
                                              const edit_entity_store_t *entities,
                                              const edit_selection_t *selection,
                                              uint32_t active_object_id,
                                              const mat4_t *view,
                                              const mat4_t *proj,
                                              const vec3_t *eye_pos,
                                              float fov_y,
                                              int fbo_height,
                                              shading_mode_t shading_mode) {
    if (!state || !state->initialized || !entities || !selection) return;
    if (edit_selection_count(selection) == 0) return;

    /* Bind flat (unlit) shader for selection outlines so they remain
     * consistently visible regardless of lighting conditions. */
    shader_program_bind(&state->flat_shader);
    shader_uniform_set_mat4(&state->flat_uniforms, &state->flat_shader,
                             "u_view", view->m, GL_FALSE);
    shader_uniform_set_mat4(&state->flat_uniforms, &state->flat_shader,
                             "u_projection", proj->m, GL_FALSE);

    bool wireframe = (shading_mode == SHADING_MODE_WIREFRAME);
    bool use_stencil = !wireframe && state->glStencilFunc
                       && state->glStencilOp && state->glStencilMask
                       && state->glColorMask;

    if (wireframe) {
        /* In wireframe mode, draw selected objects as yellow wireframe
         * on top of the existing wireframe (no scale-up needed). */
        if (state->glPolygonMode) {
            state->glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        }
        if (state->glLineWidth) {
            state->glLineWidth(OUTLINE_WIRE_WIDTH);
        }
        state->glDisable(GL_DEPTH_TEST);
    }

    uint32_t capacity = entities->capacity;

    if (use_stencil) {
        /* Stencil-based outline: works for all mesh types including
         * smooth convex shapes (capsules, spheres) where the
         * back-face-culling technique fails.
         *
         * Pass 1: Draw all selected entities at normal scale into the
         *         stencil buffer (stencil = 1, no color output).
         * Pass 2: Draw all selected entities scaled up with outline
         *         color, but only where stencil != 1 (the border). */

        /* ---- Pass 1: fill stencil ---- */
        state->glEnable(GL_STENCIL_TEST);
        state->glStencilFunc(GL_ALWAYS, 1, 0xFF);
        state->glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
        state->glStencilMask(0xFF);
        state->glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        state->glDepthMask(GL_FALSE);
        /* Disable depth test so stencil is written for all rasterized
         * fragments.  The entity's depth already occupies the buffer
         * so GL_LESS would reject every fragment at the same depth. */
        state->glDisable(GL_DEPTH_TEST);

        for (uint32_t i = 0; i < capacity; ++i) {
            if (!edit_selection_contains(selection, i)) continue;
            const edit_entity_t *ent = edit_entity_store_get(entities, i);
            if (!ent) continue;

            const static_mesh_t *mesh;
            if (ent->type == EDIT_ENTITY_TYPE_MESH) {
                mesh = viewport_render_get_entity_mesh(state, i);
            } else {
                mesh = viewport_render_get_primitive_mesh(ent->type, state);
            }
            if (!mesh) continue;

            mat4_t model = build_outline_model(ent, 1.0f);
            shader_uniform_set_mat4(&state->flat_uniforms, &state->flat_shader,
                                     "u_model", model.m, GL_FALSE);

            static_mesh_bind(mesh);
            for (uint32_t s = 0; s < mesh->submesh_count; ++s) {
                static_mesh_draw_submesh(mesh, s);
            }
        }
        static_mesh_unbind();

        /* ---- Pass 2: draw outline where stencil == 0 ---- */
        state->glStencilFunc(GL_NOTEQUAL, 1, 0xFF);
        state->glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
        state->glStencilMask(0x00);
        state->glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        /* Re-enable depth test so outlines respect other objects'
         * depth (don't bleed through geometry in front). */
        state->glEnable(GL_DEPTH_TEST);

        for (uint32_t i = 0; i < capacity; ++i) {
            if (!edit_selection_contains(selection, i)) continue;
            const edit_entity_t *ent = edit_entity_store_get(entities, i);
            if (!ent) continue;

            const static_mesh_t *mesh;
            if (ent->type == EDIT_ENTITY_TYPE_MESH) {
                mesh = viewport_render_get_entity_mesh(state, i);
            } else {
                mesh = viewport_render_get_primitive_mesh(ent->type, state);
            }
            if (!mesh) continue;

            const float *color = (i == active_object_id)
                ? OUTLINE_ACTIVE_COLOR : OUTLINE_COLOR;
            shader_uniform_set_vec3(&state->flat_uniforms, &state->flat_shader,
                                     "u_color", color);

            float scale = compute_outline_scale_(ent, eye_pos, fov_y,
                                                   fbo_height);
            mat4_t model = build_outline_model(ent, scale);
            shader_uniform_set_mat4(&state->flat_uniforms, &state->flat_shader,
                                     "u_model", model.m, GL_FALSE);

            static_mesh_bind(mesh);
            for (uint32_t s = 0; s < mesh->submesh_count; ++s) {
                static_mesh_draw_submesh(mesh, s);
            }
        }
        static_mesh_unbind();

        /* Restore GL state. */
        state->glStencilMask(0xFF);
        state->glDisable(GL_STENCIL_TEST);
        state->glDepthMask(GL_TRUE);
        state->glEnable(GL_DEPTH_TEST);
    } else if (!wireframe) {
        /* Fallback if stencil functions not available: use back-face
         * culling technique (may not work for all mesh types). */
        state->glEnable(GL_CULL_FACE);
        state->glCullFace(GL_FRONT);
        state->glDepthMask(GL_FALSE);

        for (uint32_t i = 0; i < capacity; ++i) {
            if (!edit_selection_contains(selection, i)) continue;
            const edit_entity_t *ent = edit_entity_store_get(entities, i);
            if (!ent) continue;

            const static_mesh_t *mesh;
            if (ent->type == EDIT_ENTITY_TYPE_MESH) {
                mesh = viewport_render_get_entity_mesh(state, i);
            } else {
                mesh = viewport_render_get_primitive_mesh(ent->type, state);
            }
            if (!mesh) continue;

            const float *color = (i == active_object_id)
                ? OUTLINE_ACTIVE_COLOR : OUTLINE_COLOR;
            shader_uniform_set_vec3(&state->flat_uniforms, &state->flat_shader,
                                     "u_color", color);

            float scale = compute_outline_scale_(ent, eye_pos, fov_y,
                                                   fbo_height);
            mat4_t model = build_outline_model(ent, scale);
            shader_uniform_set_mat4(&state->flat_uniforms, &state->flat_shader,
                                     "u_model", model.m, GL_FALSE);

            static_mesh_bind(mesh);
            for (uint32_t s = 0; s < mesh->submesh_count; ++s) {
                static_mesh_draw_submesh(mesh, s);
            }
        }
        static_mesh_unbind();

        state->glCullFace(GL_BACK);
        state->glDepthMask(GL_TRUE);
    } else {
        /* Wireframe mode: draw with wireframe overlay. */
        for (uint32_t i = 0; i < capacity; ++i) {
            if (!edit_selection_contains(selection, i)) continue;
            const edit_entity_t *ent = edit_entity_store_get(entities, i);
            if (!ent) continue;

            const static_mesh_t *mesh;
            if (ent->type == EDIT_ENTITY_TYPE_MESH) {
                mesh = viewport_render_get_entity_mesh(state, i);
            } else {
                mesh = viewport_render_get_primitive_mesh(ent->type, state);
            }
            if (!mesh) continue;

            const float *color = (i == active_object_id)
                ? OUTLINE_WIRE_ACTIVE_COLOR : OUTLINE_WIRE_COLOR;
            shader_uniform_set_vec3(&state->flat_uniforms, &state->flat_shader,
                                     "u_color", color);

            mat4_t model = build_outline_model(ent, 1.0f);
            shader_uniform_set_mat4(&state->flat_uniforms, &state->flat_shader,
                                     "u_model", model.m, GL_FALSE);

            static_mesh_bind(mesh);
            for (uint32_t s = 0; s < mesh->submesh_count; ++s) {
                static_mesh_draw_submesh(mesh, s);
            }
        }
        static_mesh_unbind();
    }

    if (wireframe) {
        /* Restore solid polygon mode and depth test. */
        if (state->glPolygonMode) {
            state->glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        }
        if (state->glLineWidth) {
            state->glLineWidth(1.0f);
        }
        state->glEnable(GL_DEPTH_TEST);
    }
}

/* ---- Box Select Rectangle ---- */

/** Box select rectangle color (white, semi-transparent feel via thin lines). */
static const float BOX_SELECT_COLOR[3] = {1.0f, 1.0f, 1.0f};

void viewport_render_draw_box_select(viewport_render_state_t *state,
                                       float x0, float y0,
                                       float x1, float y1) {
    if (!state || !state->initialized) return;

    /* Convert normalized [0,1] viewport coords to NDC [-1,1].
     * Viewport Y is top-down (0=top, 1=bottom). The FBO is displayed
     * with its bottom at the top (GL UV origin), so NDC Y maps directly:
     * viewport 0 → NDC -1 (bottom of FBO = top of display). */
    float nx0 = x0 * 2.0f - 1.0f;
    float ny0 = y0 * 2.0f - 1.0f;
    float nx1 = x1 * 2.0f - 1.0f;
    float ny1 = y1 * 2.0f - 1.0f;

    /* 4 line segments = 8 vertices, 6 floats each (pos + color). */
    float r = BOX_SELECT_COLOR[0];
    float g = BOX_SELECT_COLOR[1];
    float b = BOX_SELECT_COLOR[2];
    float verts[8 * 6] = {
        nx0, ny0, 0.0f, r, g, b,   nx1, ny0, 0.0f, r, g, b,
        nx1, ny0, 0.0f, r, g, b,   nx1, ny1, 0.0f, r, g, b,
        nx1, ny1, 0.0f, r, g, b,   nx0, ny1, 0.0f, r, g, b,
        nx0, ny1, 0.0f, r, g, b,   nx0, ny0, 0.0f, r, g, b,
    };

    /* Use identity VP so verts are directly in NDC. */
    mat4_t identity = mat4_identity();
    shader_program_bind(&state->grid_shader);
    shader_uniform_set_mat4(&state->grid_uniforms, &state->grid_shader,
                             "u_vp", identity.m, GL_FALSE);

    state->overlay_vao.glBindVertexArray(state->overlay_vao.handle);
    vbo_upload(&state->overlay_vbo, GL_ARRAY_BUFFER, verts, sizeof(verts),
               GL_DYNAMIC_DRAW);

    /* Bind attribute pointers for overlay VAO. */
    vao_attribute_t attrs[2] = {
        {0, 3, GL_FLOAT, GL_FALSE, 0,                 0},
        {1, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), 0},
    };
    vao_bind_attributes(&state->overlay_vao, &state->overlay_vbo, attrs, 2,
                        6 * sizeof(float));

    state->glDisable(GL_DEPTH_TEST);
    state->glDrawArrays(GL_LINES, 0, 8);
    state->glEnable(GL_DEPTH_TEST);

    state->overlay_vao.glBindVertexArray(0);
}
