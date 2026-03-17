/**
 * @file scene_viewport_collision_overlay.c
 * @brief Draw collision geometry wireframe overlay in the viewport.
 *
 * Renders the collision shape for every visible entity as a green
 * wireframe overlay on top of the solid geometry. For MESH entities,
 * uses the collision mesh if loaded, otherwise the render mesh. For
 * primitives (BOX, SPHERE, CAPSULE), uses the built-in primitive mesh.
 * HALFSPACE and MARKER entities are skipped.
 *
 * Non-static functions (1 / 4 limit):
 *   viewport_render_draw_collision_overlay
 */

#include "ferrum/editor/scene/scene_viewport_render.h"
#include "ferrum/editor/edit_entity.h"

#include "ferrum/renderer/gl_constants.h"
#include "ferrum/math/mat4.h"
#include "ferrum/math/quat.h"

#include <string.h>

/** Collision wireframe color: bright green. */
static const float COLLISION_COLOR[3] = {0.0f, 0.8f, 0.0f};

/**
 * @brief Build a model matrix from entity position, orientation, scale,
 *        and pivot offset.
 *
 * Identical to the one in scene_viewport_draw.c — duplicated here
 * because it is static there. Model = T(pos) * R * S * T(-pivot).
 */
static mat4_t build_model_matrix_(const edit_entity_t *ent) {
    mat4_t pivot_shift = mat4_translation(
        -ent->pivot_offset[0], -ent->pivot_offset[1],
        -ent->pivot_offset[2]);
    mat4_t scale = mat4_scaling(ent->scale[0], ent->scale[1], ent->scale[2]);
    mat4_t rot;
    quat_to_mat4(ent->orientation, &rot);
    mat4_t trans = mat4_translation(ent->pos[0], ent->pos[1], ent->pos[2]);

    return mat4_mul(trans, mat4_mul(rot, mat4_mul(scale, pivot_shift)));
}

void viewport_render_draw_collision_overlay(viewport_render_state_t *state,
                                             const edit_entity_store_t *entities,
                                             const mat4_t *view,
                                             const mat4_t *proj) {
    if (!state || !state->initialized || !entities) return;
    if (!state->glPolygonMode) return;

    /* Set up GL state for wireframe overlay. */
    shader_program_bind(&state->flat_shader);
    shader_uniform_set_mat4(&state->flat_uniforms, &state->flat_shader,
                             "u_view", view->m, GL_FALSE);
    shader_uniform_set_mat4(&state->flat_uniforms, &state->flat_shader,
                             "u_projection", proj->m, GL_FALSE);
    shader_uniform_set_vec3(&state->flat_uniforms, &state->flat_shader,
                             "u_color", COLLISION_COLOR);

    /* Draw wireframe on top of solid geometry: disable depth test so
     * the wireframe is always visible on the entity's surface, and
     * disable depth write to avoid polluting the depth buffer. */
    state->glDisable(GL_DEPTH_TEST);
    state->glDepthMask(GL_FALSE);
    state->glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

    /* Iterate all active entities and draw collision geometry. */
    uint32_t capacity = entities->capacity;
    for (uint32_t i = 0; i < capacity; ++i) {
        const edit_entity_t *ent = edit_entity_store_get(entities, i);
        if (!ent) continue;
        if (ent->hidden) continue;

        /* Skip types with no meaningful collision wireframe. */
        if (ent->type == EDIT_ENTITY_TYPE_HALFSPACE) continue;
        if (ent->type == EDIT_ENTITY_TYPE_MARKER) continue;

        /* Resolve collision geometry mesh:
         * MESH entities: collision mesh if loaded, else render mesh.
         * Primitives: built-in primitive mesh. */
        const static_mesh_t *mesh = NULL;
        if (ent->type == EDIT_ENTITY_TYPE_MESH) {
            mesh = viewport_render_get_collision_mesh(state, i);
            if (!mesh) {
                mesh = viewport_render_get_entity_mesh(state, i);
            }
        } else {
            mesh = viewport_render_get_primitive_mesh(ent->type, state);
        }
        if (!mesh) continue;

        /* Build model matrix and draw. */
        mat4_t model = build_model_matrix_(ent);
        shader_uniform_set_mat4(&state->flat_uniforms, &state->flat_shader,
                                 "u_model", model.m, GL_FALSE);

        static_mesh_bind(mesh);
        for (uint32_t s = 0; s < mesh->submesh_count; ++s) {
            static_mesh_draw_submesh(mesh, s);
        }
    }
    static_mesh_unbind();

    /* Restore GL state. */
    state->glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    state->glDepthMask(GL_TRUE);
    state->glEnable(GL_DEPTH_TEST);
}
