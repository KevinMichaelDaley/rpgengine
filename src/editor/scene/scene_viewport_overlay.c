/**
 * @file scene_viewport_overlay.c
 * @brief Viewport overlay rendering: 3D cursor and selection outline.
 *
 * Draws overlay geometry after the main entity pass. The 3D cursor
 * is rendered without depth test (always visible). Selection outlines
 * are drawn as slightly scaled-up solid meshes behind the selected
 * entities to create a visible border effect.
 *
 * Non-static functions (2 / 4 limit):
 *   viewport_render_draw_cursor
 *   viewport_render_draw_selection_outline
 */

#include "ferrum/editor/scene/scene_viewport_render.h"
#include "ferrum/editor/scene/scene_main.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_selection.h"

#include "ferrum/renderer/gl_constants.h"
#include "ferrum/math/mat4.h"
#include "ferrum/math/vec3.h"

/* ---- Constants ---- */

/** 3D cursor arm length (world units in each direction). */
#define CURSOR_ARM_LENGTH 0.5f

/** Selection outline color (orange). */
static const float OUTLINE_COLOR[3] = {1.0f, 0.5f, 0.0f};

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

    /* Re-upload cursor vertices to grid VBO (grid already drawn). */
    state->grid_vao.glBindVertexArray(state->grid_vao.handle);
    vbo_upload(&state->grid_vbo, GL_ARRAY_BUFFER, verts, sizeof(verts),
               GL_DYNAMIC_DRAW);

    /* Draw without depth test so cursor is always visible. */
    state->glDisable(GL_DEPTH_TEST);
    state->glDrawArrays(GL_LINES, 0, 6);
    state->glEnable(GL_DEPTH_TEST);

    state->grid_vao.glBindVertexArray(0);
}

/* ---- Selection Outline ---- */

/**
 * @brief Build a model matrix from entity transform with scale factor.
 */
static mat4_t build_outline_model(const edit_entity_t *ent, float extra_scale) {
    float deg_to_rad = 3.14159265358979323846f / 180.0f;
    mat4_t scale = mat4_scaling(
        ent->scale[0] * extra_scale,
        ent->scale[1] * extra_scale,
        ent->scale[2] * extra_scale);
    mat4_t rot_x = mat4_rotation_x(ent->rot[0] * deg_to_rad);
    mat4_t rot_y = mat4_rotation_y(ent->rot[1] * deg_to_rad);
    mat4_t rot_z = mat4_rotation_z(ent->rot[2] * deg_to_rad);
    mat4_t trans = mat4_translation(ent->pos[0], ent->pos[1], ent->pos[2]);
    mat4_t rot = mat4_mul(rot_y, mat4_mul(rot_x, rot_z));
    return mat4_mul(trans, mat4_mul(rot, scale));
}

void viewport_render_draw_selection_outline(viewport_render_state_t *state,
                                              const edit_entity_store_t *entities,
                                              const edit_selection_t *selection,
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

    /* Use the outline color with flat shading (light from above). */
    shader_uniform_set_vec3(&state->uniforms, &state->shader,
                             "u_color", OUTLINE_COLOR);
    float flat_light[3] = {0.0f, 1.0f, 0.0f};
    shader_uniform_set_vec3(&state->uniforms, &state->shader,
                             "u_light_dir", flat_light);
    vec3_t zero_eye = {0.0f, 0.0f, 0.0f};
    float ze[3] = {zero_eye.x, zero_eye.y, zero_eye.z};
    shader_uniform_set_vec3(&state->uniforms, &state->shader,
                             "u_eye_pos", ze);

    /* Draw each selected entity slightly scaled up.
     * The original entity (drawn previously) will overdraw the center,
     * leaving only the outline edges visible. */
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

        mat4_t model = build_outline_model(ent, OUTLINE_SCALE);
        shader_uniform_set_mat4(&state->uniforms, &state->shader,
                                 "u_model", model.m, GL_FALSE);

        static_mesh_bind(mesh);
        for (uint32_t s = 0; s < mesh->submesh_count; ++s) {
            static_mesh_draw_submesh(mesh, s);
        }
    }
    static_mesh_unbind();
}
