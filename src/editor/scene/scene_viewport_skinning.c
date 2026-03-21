/**
 * @file scene_viewport_skinning.c
 * @brief Viewport skinning: bone palette + shader for skeletal mesh rendering.
 *
 * Provides init/destroy for the skinning shader and bone palette buffer,
 * plus a draw function that computes bone matrices (rest_world * IBM),
 * uploads them, and renders with the skinning shader.
 *
 * Non-static functions (3 / 4 limit):
 *   1. viewport_skinning_init
 *   2. viewport_skinning_destroy
 *   3. viewport_skinning_draw
 */

#include "ferrum/editor/scene/scene_viewport_render.h"
#include "ferrum/editor/edit_skeleton_registry.h"
#include "ferrum/editor/viewport/viewport_shading.h"
#include "ferrum/renderer/mesh/skeletal_mesh.h"
#include "ferrum/renderer/gl_constants.h"
#include "ferrum/math/mat4.h"

#include <stdio.h>
#include <string.h>

/* ---- Skinning shader sources ---- */

/** Maximum bones supported in the viewport bone palette UBO. */
#define VP_SKIN_MAX_BONES 128u

/** UBO binding point for the bone palette. */
#define VP_SKIN_PALETTE_BINDING 2u

/**
 * Viewport skinning vertex shader.
 *
 * Bone weights/indices at locations 6/7 (matching skeletal_mesh_t VBO layout).
 * Uses a std140 UBO for the bone palette. The skin matrix transforms vertices
 * from bind pose to world space. Entity model matrix is applied on top.
 */
static const char *const SKIN_VERT_SRC =
    "#version 330 core\n"
    "layout(location = 0) in vec3 in_position;\n"
    "layout(location = 1) in vec3 in_normal;\n"
    "layout(location = 2) in vec2 in_uv;\n"
    "layout(location = 6) in vec4 in_bone_weights;\n"
    "layout(location = 7) in ivec4 in_bone_indices;\n"
    "\n"
    "layout(std140) uniform BonePalette {\n"
    "    mat4 bones[128];\n"
    "};\n"
    "\n"
    "uniform mat4 u_model;\n"
    "uniform mat4 u_view;\n"
    "uniform mat4 u_projection;\n"
    "\n"
    "out vec3 v_world_pos;\n"
    "out vec3 v_normal;\n"
    "\n"
    "void main() {\n"
    "    mat4 skin = bones[in_bone_indices.x] * in_bone_weights.x +\n"
    "               bones[in_bone_indices.y] * in_bone_weights.y +\n"
    "               bones[in_bone_indices.z] * in_bone_weights.z +\n"
    "               bones[in_bone_indices.w] * in_bone_weights.w;\n"
    "    vec4 skinned = skin * vec4(in_position, 1.0);\n"
    "    vec4 world = u_model * skinned;\n"
    "    v_world_pos = world.xyz;\n"
    "    v_normal = mat3(u_model) * mat3(skin) * in_normal;\n"
    "    gl_Position = u_projection * u_view * world;\n"
    "}\n";

/**
 * Viewport skinning fragment shader: Blinn-Phong (matches entity shader).
 */
static const char *const SKIN_FRAG_SRC =
    "#version 330 core\n"
    "in vec3 v_world_pos;\n"
    "in vec3 v_normal;\n"
    "uniform vec3 u_color;\n"
    "uniform vec3 u_light_dir;\n"
    "uniform vec3 u_eye_pos;\n"
    "uniform vec3 u_select_color;\n"
    "uniform float u_select_tint;\n"
    "out vec4 frag_color;\n"
    "void main() {\n"
    "    vec3 n = normalize(v_normal);\n"
    "    vec3 l = normalize(u_light_dir);\n"
    "    float diff = max(dot(n, l), 0.0);\n"
    "    vec3 v = normalize(u_eye_pos - v_world_pos);\n"
    "    vec3 h = normalize(l + v);\n"
    "    float spec = pow(max(dot(n, h), 0.0), 32.0);\n"
    "    vec3 ambient = 0.15 * u_color;\n"
    "    vec3 result = ambient + diff * u_color + 0.3 * spec * vec3(1.0);\n"
    "    result = mix(result, u_select_color, u_select_tint);\n"
    "    frag_color = vec4(result, 1.0);\n"
    "}\n";

/**
 * Skinned matcap fragment shader: half-lambert clay appearance.
 * Same lighting as the unskinned matcap shader.
 */
static const char *const SKIN_MATCAP_FRAG_SRC =
    "#version 330 core\n"
    "in vec3 v_world_pos;\n"
    "in vec3 v_normal;\n"
    "uniform vec3 u_color;\n"
    "uniform vec3 u_light_dir;\n"
    "uniform vec3 u_eye_pos;\n"
    "uniform vec3 u_select_color;\n"
    "uniform float u_select_tint;\n"
    "out vec4 frag_color;\n"
    "void main() {\n"
    "    vec3 base = u_color * vec3(0.92, 0.88, 0.82);\n"
    "    vec3 n = normalize(v_normal);\n"
    "    vec3 l = normalize(u_light_dir);\n"
    "    float half_lambert = dot(n, l) * 0.5 + 0.5;\n"
    "    half_lambert = half_lambert * half_lambert;\n"
    "    float fill = dot(n, vec3(0.0, -1.0, 0.0)) * 0.5 + 0.5;\n"
    "    fill = fill * 0.08;\n"
    "    vec3 v = normalize(u_eye_pos - v_world_pos);\n"
    "    float fresnel = 1.0 - max(dot(n, v), 0.0);\n"
    "    fresnel = fresnel * fresnel * 0.35;\n"
    "    vec3 ambient = 0.2 * base;\n"
    "    vec3 diffuse = half_lambert * base * 0.7;\n"
    "    vec3 warm_fill = fill * vec3(0.95, 0.85, 0.75);\n"
    "    vec3 rim = fresnel * vec3(0.85, 0.85, 0.9);\n"
    "    vec3 result = ambient + diffuse + warm_fill + rim;\n"
    "    result = mix(result, u_select_color, u_select_tint);\n"
    "    frag_color = vec4(result, 1.0);\n"
    "}\n";

/* ---- GL proc loader ---- */

#define LOAD_GL_PROC(field, loader_ptr, name)                 \
    do {                                                       \
        void *raw_ = (loader_ptr)->get_proc_address(           \
            (name), (loader_ptr)->user_data);                  \
        memcpy(&(field), &raw_, sizeof(field));                \
    } while (0)

/* GL functions needed for UBO block binding. */
typedef int32_t (*glGetUniformBlockIndex_fn)(uint32_t program,
                                             const char *name);
typedef void (*glUniformBlockBinding_fn)(uint32_t program,
                                         uint32_t block_index,
                                         uint32_t binding_point);

/* ---- Public API ---- */

bool viewport_skinning_init(viewport_render_state_t *state) {
    if (!state) return false;
    if (state->skin_initialized) return true;

    char log[512];

    /* Compile the skinning shader. */
    shader_program_status_t ss = shader_program_create(
        &state->skin_shader, &state->loader,
        SKIN_VERT_SRC, SKIN_FRAG_SRC, log, sizeof(log));
    if (ss != SHADER_PROGRAM_OK) {
        fprintf(stderr, "viewport_skinning: shader error: %s\n", log);
        return false;
    }
    shader_uniform_cache_init(&state->skin_uniforms, &state->skin_shader);

    /* Compile the matcap skinning shader (same vertex, matcap fragment). */
    ss = shader_program_create(
        &state->skin_matcap_shader, &state->loader,
        SKIN_VERT_SRC, SKIN_MATCAP_FRAG_SRC, log, sizeof(log));
    if (ss != SHADER_PROGRAM_OK) {
        fprintf(stderr, "viewport_skinning: matcap shader error: %s\n", log);
        shader_program_destroy(&state->skin_shader);
        return false;
    }
    shader_uniform_cache_init(&state->skin_matcap_uniforms,
                               &state->skin_matcap_shader);

    /* Bind the BonePalette uniform block to the palette binding point. */
    glGetUniformBlockIndex_fn get_block_idx = NULL;
    glUniformBlockBinding_fn set_block_bind = NULL;
    LOAD_GL_PROC(get_block_idx, &state->loader, "glGetUniformBlockIndex");
    LOAD_GL_PROC(set_block_bind, &state->loader, "glUniformBlockBinding");

    if (get_block_idx && set_block_bind) {
        uint32_t block = (uint32_t)get_block_idx(
            state->skin_shader.handle, "BonePalette");
        if (block != 0xFFFFFFFFu) {
            set_block_bind(state->skin_shader.handle,
                          block, VP_SKIN_PALETTE_BINDING);
        }
        /* Bind BonePalette on matcap skinning shader too. */
        block = (uint32_t)get_block_idx(
            state->skin_matcap_shader.handle, "BonePalette");
        if (block != 0xFFFFFFFFu) {
            set_block_bind(state->skin_matcap_shader.handle,
                          block, VP_SKIN_PALETTE_BINDING);
        }
    }

    /* Initialize bone palette buffer (prefer UBO for compatibility). */
    bone_palette_status_t bs = bone_palette_buffer_init(
        &state->bone_palette, &state->loader,
        VP_SKIN_MAX_BONES, VP_SKIN_PALETTE_BINDING,
        0 /* no SSBO — use UBO */, 0 /* no TBO */);
    if (bs != BONE_PALETTE_OK) {
        fprintf(stderr, "viewport_skinning: bone palette init failed (%d)\n",
                (int)bs);
        shader_program_destroy(&state->skin_shader);
        return false;
    }

    state->skin_initialized = true;
    return true;
}

void viewport_skinning_destroy(viewport_render_state_t *state) {
    if (!state || !state->skin_initialized) return;
    bone_palette_buffer_destroy(&state->bone_palette);
    shader_program_destroy(&state->skin_shader);
    shader_program_destroy(&state->skin_matcap_shader);
    state->skin_initialized = false;
}

void viewport_skinning_draw(viewport_render_state_t *state,
                             const edit_skeleton_entry_t *skel_entry,
                             const skeletal_mesh_t *skel_mesh,
                             const mat4_t *model,
                             const mat4_t *view,
                             const mat4_t *proj,
                             const vec3_t *eye_pos,
                             const float *color,
                             const float *select_color,
                             float select_tint,
                             shading_mode_t shading_mode) {
    if (!state || !state->skin_initialized) return;
    if (!skel_entry || !skel_mesh || !model || !view || !proj) return;

    const skeleton_def_t *skel = &skel_entry->skel;
    const mat4_t *ibms = skel_entry->ibms;
    uint32_t joint_count = skel->joint_count;

    /* Need both rest_world and IBMs to compute the bone palette. */
    if (!skel->rest_world || !ibms || joint_count == 0) return;
    if (skel_entry->ibm_count < joint_count) return;

    /* Compute bone palette: palette[i] = rest_world[i] * ibm[i].
     * This transforms vertices from bind space to skeleton-local space.
     * The entity model matrix in the shader transforms to world space. */
    uint32_t n = joint_count < VP_SKIN_MAX_BONES
                     ? joint_count : VP_SKIN_MAX_BONES;
    mat4_t palette[VP_SKIN_MAX_BONES];
    for (uint32_t i = 0; i < n; i++) {
        palette[i] = mat4_mul(skel->rest_world[i], ibms[i]);
    }
    /* Zero remaining palette entries (identity would also work). */
    for (uint32_t i = n; i < VP_SKIN_MAX_BONES; i++) {
        palette[i] = mat4_identity();
    }

    /* Upload bone palette to GPU. */
    bone_palette_buffer_update(&state->bone_palette,
                               palette,
                               VP_SKIN_MAX_BONES * sizeof(mat4_t));
    bone_palette_buffer_bind(&state->bone_palette);

    /* Select skinning shader based on shading mode. */
    shader_program_t *shader;
    shader_uniform_cache_t *ucache;
    if (shading_mode == SHADING_MODE_MATCAP) {
        shader = &state->skin_matcap_shader;
        ucache = &state->skin_matcap_uniforms;
    } else {
        shader = &state->skin_shader;
        ucache = &state->skin_uniforms;
    }
    shader_program_bind(shader);

    shader_uniform_set_mat4(ucache, shader, "u_view", view->m, GL_FALSE);
    shader_uniform_set_mat4(ucache, shader, "u_projection", proj->m, GL_FALSE);
    shader_uniform_set_mat4(ucache, shader, "u_model", model->m, GL_FALSE);

    static const float LIGHT_DIR[3] = {0.3f, 1.0f, 0.5f};
    shader_uniform_set_vec3(ucache, shader, "u_light_dir", LIGHT_DIR);
    float eye[3] = {eye_pos->x, eye_pos->y, eye_pos->z};
    shader_uniform_set_vec3(ucache, shader, "u_eye_pos", eye);
    shader_uniform_set_vec3(ucache, shader, "u_color", color);

    /* Set selection tint uniforms. */
    if (select_color) {
        shader_uniform_set_vec3(ucache, shader, "u_select_color", select_color);
    }
    shader_uniform_set_float(ucache, shader, "u_select_tint", select_tint);

    /* Bind skeletal mesh (includes bone weight/index VBOs). */
    skeletal_mesh_bind(skel_mesh);
    for (uint32_t s = 0; s < skel_mesh->base.submesh_count; s++) {
        skeletal_mesh_draw_submesh(skel_mesh, s);
    }
    skeletal_mesh_unbind();
}
