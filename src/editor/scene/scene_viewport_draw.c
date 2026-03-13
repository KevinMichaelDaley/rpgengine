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
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_selection.h"

#include "ferrum/renderer/gl_constants.h"
#include "ferrum/math/mat4.h"
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
static const float COLOR_SELECTED[3]  = {1.0f, 0.6f, 0.2f};

/* ---- Primitive mesh cache ---- */

/**
 * @brief Lazily-created primitive meshes for entity rendering.
 *
 * Created on first use and destroyed with the viewport renderer.
 * This avoids the need to store them in the mesh registry (which
 * is designed for dynamic/loaded meshes).
 */
static struct {
    static_mesh_t box;
    static_mesh_t sphere;
    static_mesh_t capsule;
    static_mesh_t plane;
    bool box_valid;
    bool sphere_valid;
    bool capsule_valid;
    bool plane_valid;
} s_primitives;

/**
 * @brief Ensure the primitive mesh for the given entity type is created.
 *
 * Returns primitive geometry for BOX, SPHERE, CAPSULE, HALFSPACE,
 * and MARKER types. Returns NULL for MESH type (those use loaded
 * FVMA geometry from the entity mesh cache, looked up separately).
 *
 * @return Pointer to the static mesh, or NULL if type has no primitive.
 */
const static_mesh_t *viewport_render_get_primitive_mesh(
    uint32_t entity_type, const gl_loader_t *loader) {
    switch (entity_type) {
    case EDIT_ENTITY_TYPE_BOX:
        if (!s_primitives.box_valid) {
            if (static_mesh_create_box(loader, 1.0f, 1.0f, 1.0f,
                                        &s_primitives.box) == 0) {
                s_primitives.box_valid = true;
            }
        }
        return s_primitives.box_valid ? &s_primitives.box : NULL;

    case EDIT_ENTITY_TYPE_SPHERE:
        if (!s_primitives.sphere_valid) {
            if (static_mesh_create_sphere(loader, 0.5f, 16, 12,
                                           &s_primitives.sphere) == 0) {
                s_primitives.sphere_valid = true;
            }
        }
        return s_primitives.sphere_valid ? &s_primitives.sphere : NULL;

    case EDIT_ENTITY_TYPE_CAPSULE:
        if (!s_primitives.capsule_valid) {
            if (static_mesh_create_capsule(loader, 0.3f, 0.5f, 16, 4,
                                            &s_primitives.capsule) == 0) {
                s_primitives.capsule_valid = true;
            }
        }
        return s_primitives.capsule_valid ? &s_primitives.capsule : NULL;

    case EDIT_ENTITY_TYPE_HALFSPACE:
        if (!s_primitives.plane_valid) {
            if (static_mesh_create_plane(loader, 10.0f, 10.0f,
                                          &s_primitives.plane) == 0) {
                s_primitives.plane_valid = true;
            }
        }
        return s_primitives.plane_valid ? &s_primitives.plane : NULL;

    case EDIT_ENTITY_TYPE_MESH:
        /* MESH entities use loaded FVMA geometry from the entity mesh
         * cache. The caller must look up the mesh separately via
         * viewport_render_get_entity_mesh(). Return NULL here so
         * unloaded MESH entities are simply not rendered. */
        return NULL;

    case EDIT_ENTITY_TYPE_MARKER:
        /* Markers are rendered as small spheres (could be wireframe
         * crosses, but solid sphere is more visible in the editor). */
        if (!s_primitives.sphere_valid) {
            if (static_mesh_create_sphere(loader, 0.5f, 16, 12,
                                           &s_primitives.sphere) == 0) {
                s_primitives.sphere_valid = true;
            }
        }
        return s_primitives.sphere_valid ? &s_primitives.sphere : NULL;

    default:
        /* Unknown type: use box as fallback. */
        if (!s_primitives.box_valid) {
            if (static_mesh_create_box(loader, 1.0f, 1.0f, 1.0f,
                                        &s_primitives.box) == 0) {
                s_primitives.box_valid = true;
            }
        }
        return s_primitives.box_valid ? &s_primitives.box : NULL;
    }
}

/**
 * @brief Get the color for an entity based on its type and selection state.
 */
static const float *get_entity_color(uint32_t entity_type, bool selected) {
    if (selected) return COLOR_SELECTED;

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
 * @brief Build a model matrix from entity position, rotation, and scale.
 *
 * Rotation uses euler angles in degrees (pitch, yaw, roll) applied
 * in Y-X-Z order.
 */
static mat4_t build_model_matrix(const edit_entity_t *ent) {
    float deg_to_rad = 3.14159265358979323846f / 180.0f;

    mat4_t scale = mat4_scaling(ent->scale[0], ent->scale[1], ent->scale[2]);
    mat4_t rot_x = mat4_rotation_x(ent->rot[0] * deg_to_rad);
    mat4_t rot_y = mat4_rotation_y(ent->rot[1] * deg_to_rad);
    mat4_t rot_z = mat4_rotation_z(ent->rot[2] * deg_to_rad);
    mat4_t trans = mat4_translation(ent->pos[0], ent->pos[1], ent->pos[2]);

    /* T * Ry * Rx * Rz * S */
    mat4_t rot = mat4_mul(rot_y, mat4_mul(rot_x, rot_z));
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
                                    const mat4_t *view, const mat4_t *proj,
                                    const vec3_t *eye_pos) {
    if (!state || !state->initialized || !entities) return;

    /* Bind entity shader and set per-frame uniforms. */
    shader_program_bind(&state->shader);
    shader_uniform_set_mat4(&state->uniforms, &state->shader,
                             "u_view", view->m, GL_FALSE);
    shader_uniform_set_mat4(&state->uniforms, &state->shader,
                             "u_projection", proj->m, GL_FALSE);
    shader_uniform_set_vec3(&state->uniforms, &state->shader,
                             "u_light_dir", LIGHT_DIR);
    float eye[3] = {eye_pos->x, eye_pos->y, eye_pos->z};
    shader_uniform_set_vec3(&state->uniforms, &state->shader,
                             "u_eye_pos", eye);

    /* Iterate all active entities and draw each with its mesh. */
    uint32_t capacity = entities->capacity;
    for (uint32_t i = 0; i < capacity; ++i) {
        const edit_entity_t *ent = edit_entity_store_get(entities, i);
        if (!ent) continue;

        /* Resolve the mesh for this entity.
         * MESH type entities use loaded FVMA geometry from the entity
         * mesh cache. All other types use built-in primitives. */
        const static_mesh_t *mesh;
        if (ent->type == EDIT_ENTITY_TYPE_MESH) {
            mesh = viewport_render_get_entity_mesh(state, i);
        } else {
            mesh = viewport_render_get_primitive_mesh(ent->type, &state->loader);
        }
        if (!mesh) continue;

        /* Build model matrix from entity transform. */
        mat4_t model = build_model_matrix(ent);
        shader_uniform_set_mat4(&state->uniforms, &state->shader,
                                 "u_model", model.m, GL_FALSE);

        /* Set entity color (selection-aware). */
        bool selected = selection
            ? edit_selection_contains(selection, i) : false;
        const float *color = get_entity_color(ent->type, selected);
        shader_uniform_set_vec3(&state->uniforms, &state->shader,
                                 "u_color", color);

        /* Draw the mesh (all submeshes). */
        static_mesh_bind(mesh);
        for (uint32_t s = 0; s < mesh->submesh_count; ++s) {
            static_mesh_draw_submesh(mesh, s);
        }
    }
    static_mesh_unbind();
}

void viewport_render_draw_scene(struct scene_editor *ed) {
    if (!ed) return;
    viewport_render_state_t *vp = &ed->viewport;
    if (!vp->initialized) return;

    /* Resize FBO if panel size changed. */
    panel_rect_t rect = panel_layout_get_rect(&ed->layout, PANEL_VIEWPORT);
    if (rect.w > 0 && rect.h > 0) {
        viewport_render_resize(vp, rect.w, rect.h);
    }

    /* Compute camera matrices. */
    float aspect = (vp->fbo_width > 0 && vp->fbo_height > 0)
        ? (float)vp->fbo_width / (float)vp->fbo_height : 1.0f;
    mat4_t view, proj;
    editor_camera_view_matrix(&vp->camera, &view);
    editor_camera_projection_matrix(&vp->camera, aspect, &proj);
    vec3_t eye_pos = editor_camera_eye_position(&vp->camera);

    /* Bind the viewport FBO. */
    vp->glBindFramebuffer(GL_FRAMEBUFFER, vp->fbo);
    vp->glViewport(0, 0, vp->fbo_width, vp->fbo_height);
    vp->glClearColor(0.12f, 0.12f, 0.14f, 1.0f);
    vp->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    vp->glEnable(GL_DEPTH_TEST);
    vp->glEnable(GL_CULL_FACE);

    /* Draw grid. */
    viewport_render_draw_grid(vp, &view, &proj);

    /* Draw all entities. */
    viewport_render_draw_entities(vp, &ed->entities, &ed->selection,
                                   &view, &proj, &eye_pos);

    /* Draw selection outlines (wireframe, slightly scaled up). */
    viewport_render_draw_selection_outline(vp, &ed->entities, &ed->selection,
                                            &view, &proj);

    /* Draw 3D cursor crosshair. */
    viewport_render_draw_cursor(vp, &ed->cursor_3d, &view, &proj);

    /* Unbind FBO (restore default framebuffer). */
    vp->glDisable(GL_CULL_FACE);
    vp->glDisable(GL_DEPTH_TEST);
    vp->glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

/**
 * @brief Destroy lazily-created primitive meshes.
 *
 * Called from viewport_render_destroy() to clean up static primitives.
 */
void viewport_render_destroy_primitives(void) {
    if (s_primitives.box_valid)     static_mesh_destroy(&s_primitives.box);
    if (s_primitives.sphere_valid)  static_mesh_destroy(&s_primitives.sphere);
    if (s_primitives.capsule_valid) static_mesh_destroy(&s_primitives.capsule);
    if (s_primitives.plane_valid)   static_mesh_destroy(&s_primitives.plane);
    memset(&s_primitives, 0, sizeof(s_primitives));
}
