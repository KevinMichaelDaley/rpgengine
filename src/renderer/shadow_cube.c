/**
 * @file shadow_cube.c
 * @brief Point-light cube shadow map (see shadow_cube.h).
 */
#include "ferrum/renderer/shadow_cube.h"

#include <string.h>

#include "ferrum/math/mat4.h"
#include "ferrum/math/vec3.h"
#include "ferrum/renderer/cull/frustum_cull.h"
#include "ferrum/renderer/gl_constants.h"
#include "ferrum/renderer/mesh/static_mesh.h"

/* Vertex: world-space position for the distance write + clip position (composed
 * on the GPU, matching the PBR/depth-prepass transform). */
/* Single-pass layered render: one instanced draw emits all 6 faces. The VS
 * writes gl_Layer per instance (GL_ARB_shader_viewport_layer_array), routing each
 * instance to its cube-array layer, and picks that face's view-proj. */
static const char *const SC_VS =
    "#version 330 core\n"
    "#extension GL_ARB_shader_viewport_layer_array : require\n"
    "layout(location=0) in vec3 in_position;\n"
    "uniform mat4 u_model;\n"
    "uniform mat4 u_faceVP[6];\n"
    "uniform int u_layer_base;\n"
    "out vec3 v_world;\n"
    "void main(){\n"
    "  vec4 wp = u_model * vec4(in_position, 1.0);\n"
    "  v_world = wp.xyz;\n"
    "  gl_Layer = u_layer_base + gl_InstanceID;\n"
    "  gl_Position = u_faceVP[gl_InstanceID] * wp;\n"
    "}\n";
/* Fragment: linear light->fragment distance, normalised by the far plane. */
static const char *const SC_FS =
    "#version 330 core\n"
    "in vec3 v_world;\n"
    "uniform vec3 u_light_pos;\n"
    "uniform float u_far;\n"
    "layout(location=0) out float o_dist;\n"
    "void main(){ o_dist = distance(v_world, u_light_pos) / u_far; }\n";

/* Standard GL cube-face look directions + up vectors, so a directional lookup
 * (fragment->light) in the PBR shader selects the matching face. */
static void sc_face_view(uint32_t face, const float lp[3], mat4_t *out)
{
    static const float dir[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 },
                                     { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
    static const float up[6][3] = { { 0, -1, 0 }, { 0, -1, 0 }, { 0, 0, 1 },
                                    { 0, 0, -1 }, { 0, -1, 0 }, { 0, -1, 0 } };
    vec3_t eye = { lp[0], lp[1], lp[2] };
    vec3_t tgt = { lp[0] + dir[face][0], lp[1] + dir[face][1], lp[2] + dir[face][2] };
    vec3_t u = { up[face][0], up[face][1], up[face][2] };
    mat4_look_at(eye, tgt, u, out);
}

#define SC_LOAD(dst, name)                                                    \
    do {                                                                      \
        void *p_ = loader->get_proc_address((name), loader->user_data);       \
        if (p_ == NULL) return false;                                         \
        memcpy(&(dst), &p_, sizeof(p_));                                      \
    } while (0)

bool shadow_cube_init(shadow_cube_t *sc, const gl_loader_t *loader,
                      uint32_t resolution, float near_plane, float far_plane,
                      uint32_t max_lights)
{
    if (sc == NULL || loader == NULL || loader->get_proc_address == NULL ||
        resolution == 0)
        return false;
    memset(sc, 0, sizeof(*sc));
    sc->resolution = resolution;
    sc->near_plane = near_plane;
    sc->far_plane = far_plane;
    sc->max_lights = max_lights ? max_lights : 1u;

    if (shader_program_create(&sc->shader, loader, SC_VS, SC_FS, NULL, 0) !=
        SHADER_PROGRAM_OK)
        return false;
    shader_uniform_cache_init(&sc->cache, &sc->shader);

    SC_LOAD(sc->glGenFramebuffers, "glGenFramebuffers");
    SC_LOAD(sc->glDeleteFramebuffers, "glDeleteFramebuffers");
    SC_LOAD(sc->glBindFramebuffer, "glBindFramebuffer");
    SC_LOAD(sc->glFramebufferTexture2D, "glFramebufferTexture2D");
    SC_LOAD(sc->glFramebufferTextureLayer, "glFramebufferTextureLayer");
    SC_LOAD(sc->glFramebufferTexture, "glFramebufferTexture");
    SC_LOAD(sc->glDrawElementsInstanced, "glDrawElementsInstanced");
    SC_LOAD(sc->glTexImage3D, "glTexImage3D");
    SC_LOAD(sc->glGenRenderbuffers, "glGenRenderbuffers");
    SC_LOAD(sc->glDeleteRenderbuffers, "glDeleteRenderbuffers");
    SC_LOAD(sc->glBindRenderbuffer, "glBindRenderbuffer");
    SC_LOAD(sc->glRenderbufferStorage, "glRenderbufferStorage");
    SC_LOAD(sc->glFramebufferRenderbuffer, "glFramebufferRenderbuffer");
    SC_LOAD(sc->glGenTextures, "glGenTextures");
    SC_LOAD(sc->glDeleteTextures, "glDeleteTextures");
    SC_LOAD(sc->glBindTexture, "glBindTexture");
    SC_LOAD(sc->glActiveTexture, "glActiveTexture");
    SC_LOAD(sc->glTexImage2D, "glTexImage2D");
    SC_LOAD(sc->glTexParameteri, "glTexParameteri");
    SC_LOAD(sc->glViewport, "glViewport");
    SC_LOAD(sc->glClearColor, "glClearColor");
    SC_LOAD(sc->glClear, "glClear");
    SC_LOAD(sc->glEnable, "glEnable");
    SC_LOAD(sc->glDepthFunc, "glDepthFunc");

    /* R32F cube-map ARRAY: 6*max_lights faces (linear distance / far). One cube
     * (6 consecutive array layers) per shadow-casting point light. */
    sc->glGenTextures(1, &sc->cube);
    sc->glBindTexture(GL_TEXTURE_CUBE_MAP_ARRAY, sc->cube);
    sc->glTexImage3D(GL_TEXTURE_CUBE_MAP_ARRAY, 0, GL_R32F,
                     (int32_t)resolution, (int32_t)resolution,
                     (int32_t)(6u * sc->max_lights), 0, GL_RED, GL_FLOAT, NULL);
    sc->glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    sc->glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    sc->glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    sc->glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    sc->glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    /* Layered depth 2D-array (matches the colour array's layer count) so each
     * layer gets independent depth during the single-pass layered render. */
    sc->glGenTextures(1, &sc->depth_arr);
    sc->glBindTexture(GL_TEXTURE_2D_ARRAY, sc->depth_arr);
    /* 32-bit float depth: the cube's near/far span is large (near 0.1 .. far
     * ~30 m), and 24-bit fixed-point z-fights badly on distant surfaces, banding
     * the stored linear distances. 32F depth removes the banding. */
    sc->glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT32F,
                     (int32_t)resolution, (int32_t)resolution,
                     (int32_t)(6u * sc->max_lights), 0, GL_DEPTH_COMPONENT,
                     GL_FLOAT, NULL);
    sc->glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    sc->glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    /* Layered FBO: attach whole arrays; gl_Layer (written by the VS) routes each
     * instance to its face layer. */
    sc->glGenFramebuffers(1, &sc->fbo);
    sc->glBindFramebuffer(GL_FRAMEBUFFER, sc->fbo);
    sc->glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, sc->cube, 0);
    sc->glFramebufferTexture(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, sc->depth_arr, 0);
    sc->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

/* Face-index names for the per-face view-proj uniform array (stable literals). */
static const char *const SC_FACEVP[6] = {
    "u_faceVP[0]", "u_faceVP[1]", "u_faceVP[2]",
    "u_faceVP[3]", "u_faceVP[4]", "u_faceVP[5]" };

void shadow_cube_clear(shadow_cube_t *sc)
{
    if (sc == NULL) return;
    sc->glBindFramebuffer(GL_FRAMEBUFFER, sc->fbo);
    sc->glViewport(0, 0, (int32_t)sc->resolution, (int32_t)sc->resolution);
    sc->glClearColor(1.0f, 1.0f, 1.0f, 1.0f); /* every unwritten texel == far. */
    sc->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); /* all layers at once. */
}

void shadow_cube_render_light(shadow_cube_t *sc, const render_scene_t *scene,
                              const float light_pos[3], uint32_t slot, float range)
{
    if (sc == NULL || scene == NULL || light_pos == NULL || slot >= sc->max_lights)
        return;
    mat4_t proj;
    mat4_perspective(1.57079633f, 1.0f, sc->near_plane, sc->far_plane, &proj);

    sc->glBindFramebuffer(GL_FRAMEBUFFER, sc->fbo);
    sc->glViewport(0, 0, (int32_t)sc->resolution, (int32_t)sc->resolution);
    sc->glEnable(GL_DEPTH_TEST);
    sc->glDepthFunc(GL_LESS);
    shader_program_bind(&sc->shader);
    shader_uniform_set_vec3(&sc->cache, &sc->shader, "u_light_pos", light_pos);
    shader_uniform_set_float(&sc->cache, &sc->shader, "u_far", sc->far_plane);
    shader_uniform_set_int(&sc->cache, &sc->shader, "u_layer_base", (int32_t)(slot * 6u));
    /* The 6 face view-projections for this light. */
    for (uint32_t f = 0; f < 6; ++f) {
        mat4_t view, vp;
        sc_face_view(f, light_pos, &view);
        vp = mat4_mul(proj, view);
        shader_uniform_set_mat4(&sc->cache, &sc->shader, SC_FACEVP[f], vp.m, 0);
    }
    /* One INSTANCED draw per submesh: 6 instances -> 6 faces (VS routes gl_Layer).
     * Range-cull casters outside the light's reach (rpg-9u96): a mesh farther than
     * `range` from the light contributes nothing to its shadow. */
    for (uint32_t i = 0; i < scene->count; ++i) {
        const render_renderable_t *r = &scene->items[i];
        if (r->mesh == NULL)
            continue;
        if (sphere_cull_aabb(light_pos, range, r->model,
                             r->mesh->aabb_min, r->mesh->aabb_max))
            continue;
        shader_uniform_set_mat4(&sc->cache, &sc->shader, "u_model", r->model, 0);
        static_mesh_bind(r->mesh);
        for (uint32_t s = 0; s < r->mesh->submesh_count; ++s) {
            const render_submesh_t *sub = &r->mesh->submeshes[s];
            size_t off = (size_t)sub->index_offset * sizeof(uint32_t);
            sc->glDrawElementsInstanced(GL_TRIANGLES, (int32_t)sub->index_count,
                                        GL_UNSIGNED_INT, (const void *)off, 6);
        }
    }
    sc->glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void shadow_cube_bind(const shadow_cube_t *sc, shader_uniform_cache_t *cache,
                      const shader_program_t *program, uint32_t unit)
{
    if (sc == NULL || cache == NULL || program == NULL)
        return;
    sc->glActiveTexture(GL_TEXTURE0 + unit);
    sc->glBindTexture(GL_TEXTURE_CUBE_MAP_ARRAY, sc->cube);
    shader_uniform_set_int(cache, program, "u_shadow_cube_arr", (int32_t)unit);
    shader_uniform_set_float(cache, program, "u_shadow_far", sc->far_plane);
}

void shadow_cube_destroy(shadow_cube_t *sc)
{
    if (sc == NULL)
        return;
    if (sc->glDeleteFramebuffers && sc->fbo)
        sc->glDeleteFramebuffers(1, &sc->fbo);
    if (sc->glDeleteTextures && sc->depth_arr)
        sc->glDeleteTextures(1, &sc->depth_arr);
    if (sc->glDeleteTextures && sc->cube)
        sc->glDeleteTextures(1, &sc->cube);
    shader_program_destroy(&sc->shader);
    sc->fbo = sc->depth_arr = sc->cube = 0;
}
