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
#include "ferrum/editor/edit_selection.h"

#include "ferrum/renderer/gl_constants.h"
#include "ferrum/math/mat4.h"
#include "ferrum/math/quat.h"
#include "ferrum/math/vec3.h"

/* ---- Constants ---- */

/** 3D cursor arm length (world units in each direction). */
#define CURSOR_ARM_LENGTH 0.5f

/** Selection outline color (orange). */
static const float OUTLINE_COLOR[3] = {1.0f, 0.5f, 0.0f};

/** Active object outline color (whitish-yellow). */
static const float OUTLINE_ACTIVE_COLOR[3] = {1.0f, 0.9f, 0.5f};

/** Selection outline scale factor (slightly larger than entity). */
#define OUTLINE_SCALE 1.05f

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
 */
static mat4_t build_outline_model(const edit_entity_t *ent, float extra_scale) {
    mat4_t scale = mat4_scaling(
        ent->scale[0] * extra_scale,
        ent->scale[1] * extra_scale,
        ent->scale[2] * extra_scale);
    mat4_t rot;
    quat_to_mat4(ent->orientation, &rot);
    mat4_t trans = mat4_translation(ent->pos[0], ent->pos[1], ent->pos[2]);
    return mat4_mul(trans, mat4_mul(rot, scale));
}

void viewport_render_draw_selection_outline(viewport_render_state_t *state,
                                              const edit_entity_store_t *entities,
                                              const edit_selection_t *selection,
                                              uint32_t active_object_id,
                                              const mat4_t *view,
                                              const mat4_t *proj) {
    if (!state || !state->initialized || !entities || !selection) return;
    if (edit_selection_count(selection) == 0) return;

    /* Bind entity shader. */
    shader_program_bind(&state->shader);
    shader_uniform_set_mat4(&state->uniforms, &state->shader,
                             "u_view", view->m, GL_FALSE);
    shader_uniform_set_mat4(&state->uniforms, &state->shader,
                             "u_projection", proj->m, GL_FALSE);

    float flat_light[3] = {0.0f, 1.0f, 0.0f};
    shader_uniform_set_vec3(&state->uniforms, &state->shader,
                             "u_light_dir", flat_light);
    vec3_t zero_eye = {0.0f, 0.0f, 0.0f};
    float ze[3] = {zero_eye.x, zero_eye.y, zero_eye.z};
    shader_uniform_set_vec3(&state->uniforms, &state->shader,
                             "u_eye_pos", ze);

    /* Cull front faces so only back faces of the scaled-up mesh are
     * visible. These back faces peek out around the edges of the
     * original entity, creating the outline effect. */
    state->glEnable(GL_CULL_FACE);
    state->glCullFace(GL_FRONT);

    uint32_t capacity = entities->capacity;
    for (uint32_t i = 0; i < capacity; ++i) {
        if (!edit_selection_contains(selection, i)) continue;
        const edit_entity_t *ent = edit_entity_store_get(entities, i);
        if (!ent) continue;

        const static_mesh_t *mesh;
        if (ent->type == EDIT_ENTITY_TYPE_MESH) {
            mesh = viewport_render_get_entity_mesh(state, i);
        } else {
            mesh = viewport_render_get_primitive_mesh(ent->type, &state->loader);
        }
        if (!mesh) continue;

        /* Active object gets a distinct whitish-yellow outline. */
        const float *color = (i == active_object_id)
            ? OUTLINE_ACTIVE_COLOR : OUTLINE_COLOR;
        shader_uniform_set_vec3(&state->uniforms, &state->shader,
                                 "u_color", color);

        mat4_t model = build_outline_model(ent, OUTLINE_SCALE);
        shader_uniform_set_mat4(&state->uniforms, &state->shader,
                                 "u_model", model.m, GL_FALSE);

        static_mesh_bind(mesh);
        for (uint32_t s = 0; s < mesh->submesh_count; ++s) {
            static_mesh_draw_submesh(mesh, s);
        }
    }
    static_mesh_unbind();

    /* Restore back-face culling. */
    state->glCullFace(GL_BACK);
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
