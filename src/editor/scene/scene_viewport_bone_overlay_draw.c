/**
 * @file scene_viewport_bone_overlay_draw.c
 * @brief Draw bone capsule overlay for skeleton entities.
 *
 * Renders each bone as a wireframe capsule using the flat shader.
 * Color-codes bones by selection state: light blue (unselected),
 * bright yellow (selected), white (active).
 *
 * Non-static functions (1 / 4 limit):
 *   viewport_render_draw_bone_overlay
 */

#include "ferrum/editor/scene/scene_viewport_bone_overlay.h"
#include "ferrum/editor/scene/scene_viewport_render.h"
#include "ferrum/editor/edit_bone_selection.h"
#include "ferrum/animation/constraint_params.h"

#include "ferrum/renderer/gl_constants.h"
#include "ferrum/math/mat4.h"
#include "ferrum/math/vec3.h"

#include <math.h>

/* ---- Bone color constants ---- */

/** Unselected bone: light blue. */
static const float BONE_COLOR_DEFAULT[3] = {0.4f, 0.6f, 0.9f};

/** Selected bone: bright yellow. */
static const float BONE_COLOR_SELECTED[3] = {1.0f, 0.9f, 0.2f};

/**
 * @brief Build a model matrix that places a unit capsule along the
 *        bone axis at the bone center, scaled to length and radius.
 *
 * The unit capsule mesh is assumed to be axis-aligned along Y with
 * height 1 and radius 0.5. We scale it to (radius*2, length, radius*2)
 * and orient it along the bone axis.
 */
static mat4_t build_bone_capsule_model_(const bone_capsule_params_t *p,
                                         const float *entity_model) {
    /* Scale: X/Z by radius (unit capsule has radius 0.5, so scale by
     * radius / 0.5 = radius * 2), Y by length. */
    float sx = p->radius * 2.0f;
    float sy = p->length;
    float sz = p->radius * 2.0f;

    /* Handle zero-length bones: render as a small sphere. */
    if (sy < 1e-6f) {
        sy = p->radius * 2.0f;
    }

    mat4_t scale = mat4_scaling(sx, sy, sz);

    /* Build rotation matrix to orient Y-up to bone axis. */
    mat4_t rot = mat4_identity();
    float ax = p->axis[0];
    float ay = p->axis[1];
    float az = p->axis[2];

    /* If axis is close to Y-up, rotation is identity (or flip for -Y). */
    float dot_y = ay; /* dot(axis, (0,1,0)) */
    if (dot_y > 0.9999f) {
        /* Already aligned with Y. */
        rot = mat4_identity();
    } else if (dot_y < -0.9999f) {
        /* 180-degree rotation around X or Z. */
        rot = mat4_scaling(1.0f, -1.0f, -1.0f);
    } else {
        /* General case: axis-angle rotation from Y to bone axis.
         * Cross product of Y-up and bone axis gives rotation axis. */
        float cx = az;    /* cross(Y, axis) = (az, 0, -ax) */
        float cy = 0.0f;
        float cz = -ax;
        float cross_len = sqrtf(cx * cx + cz * cz);
        if (cross_len > 1e-6f) {
            float inv = 1.0f / cross_len;
            cx *= inv;
            cz *= inv;
            /* Angle between Y and axis. */
            float angle = acosf(dot_y);
            float c = cosf(angle);
            float s = sinf(angle);
            float t = 1.0f - c;
            /* Rodrigues rotation matrix. */
            rot.m[0]  = t * cx * cx + c;
            rot.m[1]  = t * cx * cy + s * cz;
            rot.m[2]  = t * cx * cz - s * cy;
            rot.m[3]  = 0.0f;
            rot.m[4]  = t * cy * cx - s * cz;
            rot.m[5]  = t * cy * cy + c;
            rot.m[6]  = t * cy * cz + s * cx;
            rot.m[7]  = 0.0f;
            rot.m[8]  = t * cz * cx + s * cy;
            rot.m[9]  = t * cz * cy - s * cx;
            rot.m[10] = t * cz * cz + c;
            rot.m[11] = 0.0f;
            rot.m[12] = 0.0f;
            rot.m[13] = 0.0f;
            rot.m[14] = 0.0f;
            rot.m[15] = 1.0f;
        }
    }

    /* Translation to bone center. */
    mat4_t trans = mat4_translation(p->center[0], p->center[1], p->center[2]);

    /* Local model: T * R * S. */
    mat4_t local = mat4_mul(trans, mat4_mul(rot, scale));

    /* Apply entity model matrix. */
    mat4_t entity_mat;
    for (int i = 0; i < 16; i++) {
        entity_mat.m[i] = entity_model[i];
    }
    return mat4_mul(entity_mat, local);
}

void viewport_render_draw_bone_overlay(
    viewport_render_state_t *state,
    const skeleton_def_t *skel,
    uint32_t entity_id,
    const float *model,
    const edit_bone_selection_t *bone_sel) {

    if (!state || !state->initialized || !skel || !model) return;
    if (skel->joint_count == 0) return;
    if (!skel->tail_positions) return;
    if (!state->glPolygonMode) return;

    /* Get the capsule primitive mesh for bone rendering. */
    const static_mesh_t *capsule_mesh = mesh_registry_get_static(
        &state->meshes, state->mesh_capsule);
    if (!capsule_mesh) return;

    /* Set up flat shader for wireframe overlay. */
    shader_program_bind(&state->flat_shader);

    /* Disable depth test so bones overlay everything, disable depth
     * write, and switch to wireframe mode. */
    state->glDisable(GL_DEPTH_TEST);
    state->glDepthMask(GL_FALSE);
    state->glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

    static_mesh_bind(capsule_mesh);

    for (uint32_t j = 0; j < skel->joint_count; j++) {
        /* Compute head position: the world-space translation from
         * the rest_world transform matrix (column 3). */
        float head[3];
        head[0] = skel->rest_world[j].m[12];
        head[1] = skel->rest_world[j].m[13];
        head[2] = skel->rest_world[j].m[14];

        /* Tail position from the tail_positions array. */
        float tail[3];
        tail[0] = skel->tail_positions[j * 3 + 0];
        tail[1] = skel->tail_positions[j * 3 + 1];
        tail[2] = skel->tail_positions[j * 3 + 2];

        /* Compute capsule parameters. */
        bone_capsule_params_t params;
        bone_capsule_params_from_joint(head, tail, &params);

        /* Build model matrix for this bone capsule. */
        mat4_t bone_model = build_bone_capsule_model_(&params, model);
        shader_uniform_set_mat4(&state->flat_uniforms, &state->flat_shader,
                                 "u_model", bone_model.m, GL_FALSE);

        /* Choose color based on selection state. */
        const float *color = BONE_COLOR_DEFAULT;
        if (bone_sel) {
            if (edit_bone_selection_contains(bone_sel, entity_id, j)) {
                color = BONE_COLOR_SELECTED;
            }
        }
        shader_uniform_set_vec3(&state->flat_uniforms, &state->flat_shader,
                                 "u_color", color);

        /* Draw all submeshes of the capsule. */
        for (uint32_t s = 0; s < capsule_mesh->submesh_count; s++) {
            static_mesh_draw_submesh(capsule_mesh, s);
        }
    }

    static_mesh_unbind();

    /* Restore GL state. */
    state->glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    state->glDepthMask(GL_TRUE);
    state->glEnable(GL_DEPTH_TEST);
}
