/**
 * @file shadow_spot.c
 * @brief Spot-light 2D shadow map (see shadow_spot.h).
 */
#include "ferrum/renderer/shadow_spot.h"

#include <math.h>
#include <string.h>

#include "ferrum/math/vec3.h"
#include "ferrum/renderer/gl_constants.h"
#include "ferrum/renderer/mesh/static_mesh.h"

static const char *const SS_VS =
    "#version 330 core\n"
    "layout(location=0) in vec3 in_position;\n"
    "uniform mat4 u_model;\n"
    "uniform mat4 u_view;\n"
    "uniform mat4 u_projection;\n"
    "out vec3 v_world;\n"
    "void main(){\n"
    "  vec4 wp = u_model * vec4(in_position, 1.0);\n"
    "  v_world = wp.xyz;\n"
    "  gl_Position = u_projection * u_view * wp;\n"
    "}\n";
static const char *const SS_FS =
    "#version 330 core\n"
    "in vec3 v_world;\n"
    "uniform vec3 u_light_pos;\n"
    "uniform float u_far;\n"
    "layout(location=0) out float o_dist;\n"
    "void main(){ o_dist = distance(v_world, u_light_pos) / u_far; }\n";

#define SS_LOAD(dst, name)                                                    \
    do {                                                                      \
        void *p_ = loader->get_proc_address((name), loader->user_data);       \
        if (p_ == NULL) return false;                                         \
        memcpy(&(dst), &p_, sizeof(p_));                                      \
    } while (0)

bool shadow_spot_init(shadow_spot_t *ss, const gl_loader_t *loader,
                      uint32_t resolution, float near_plane, float far_plane)
{
    if (ss == NULL || loader == NULL || loader->get_proc_address == NULL ||
        resolution == 0)
        return false;
    memset(ss, 0, sizeof(*ss));
    ss->resolution = resolution;
    ss->near_plane = near_plane;
    ss->far_plane = far_plane;

    if (shader_program_create(&ss->shader, loader, SS_VS, SS_FS, NULL, 0) !=
        SHADER_PROGRAM_OK)
        return false;
    shader_uniform_cache_init(&ss->cache, &ss->shader);

    SS_LOAD(ss->glGenFramebuffers, "glGenFramebuffers");
    SS_LOAD(ss->glDeleteFramebuffers, "glDeleteFramebuffers");
    SS_LOAD(ss->glBindFramebuffer, "glBindFramebuffer");
    SS_LOAD(ss->glFramebufferTexture2D, "glFramebufferTexture2D");
    SS_LOAD(ss->glGenRenderbuffers, "glGenRenderbuffers");
    SS_LOAD(ss->glDeleteRenderbuffers, "glDeleteRenderbuffers");
    SS_LOAD(ss->glBindRenderbuffer, "glBindRenderbuffer");
    SS_LOAD(ss->glRenderbufferStorage, "glRenderbufferStorage");
    SS_LOAD(ss->glFramebufferRenderbuffer, "glFramebufferRenderbuffer");
    SS_LOAD(ss->glGenTextures, "glGenTextures");
    SS_LOAD(ss->glDeleteTextures, "glDeleteTextures");
    SS_LOAD(ss->glBindTexture, "glBindTexture");
    SS_LOAD(ss->glActiveTexture, "glActiveTexture");
    SS_LOAD(ss->glTexImage2D, "glTexImage2D");
    SS_LOAD(ss->glTexParameteri, "glTexParameteri");
    SS_LOAD(ss->glViewport, "glViewport");
    SS_LOAD(ss->glClearColor, "glClearColor");
    SS_LOAD(ss->glClear, "glClear");
    SS_LOAD(ss->glEnable, "glEnable");
    SS_LOAD(ss->glDepthFunc, "glDepthFunc");

    ss->glGenTextures(1, &ss->map);
    ss->glBindTexture(GL_TEXTURE_2D, ss->map);
    ss->glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, (int32_t)resolution,
                     (int32_t)resolution, 0, GL_RED, GL_FLOAT, NULL);
    ss->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    ss->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    ss->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    ss->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    ss->glGenRenderbuffers(1, &ss->depth_rb);
    ss->glBindRenderbuffer(GL_RENDERBUFFER, ss->depth_rb);
    ss->glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24,
                              (int32_t)resolution, (int32_t)resolution);
    ss->glGenFramebuffers(1, &ss->fbo);
    ss->glBindFramebuffer(GL_FRAMEBUFFER, ss->fbo);
    ss->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, ss->map, 0);
    ss->glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                  GL_RENDERBUFFER, ss->depth_rb);
    ss->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

void shadow_spot_render(shadow_spot_t *ss, const render_scene_t *scene,
                        const float light_pos[3], const float light_dir[3],
                        float fov_radians)
{
    if (ss == NULL || scene == NULL || light_pos == NULL || light_dir == NULL)
        return;
    vec3_t eye = { light_pos[0], light_pos[1], light_pos[2] };
    vec3_t tgt = { light_pos[0] + light_dir[0], light_pos[1] + light_dir[1],
                   light_pos[2] + light_dir[2] };
    /* Pick an up not parallel to the cone axis. */
    vec3_t up = (fabsf(light_dir[1]) < 0.99f) ? (vec3_t){ 0, 1, 0 }
                                              : (vec3_t){ 0, 0, 1 };
    mat4_t view, proj;
    mat4_look_at(eye, tgt, up, &view);
    /* Widen slightly beyond the cone so PCF near the edge has data. */
    float fov = fov_radians * 1.1f;
    if (fov > 3.0f) fov = 3.0f;
    mat4_perspective(fov, 1.0f, ss->near_plane, ss->far_plane, &proj);
    ss->view_proj = mat4_mul(proj, view);

    ss->glBindFramebuffer(GL_FRAMEBUFFER, ss->fbo);
    ss->glViewport(0, 0, (int32_t)ss->resolution, (int32_t)ss->resolution);
    ss->glEnable(GL_DEPTH_TEST);
    ss->glDepthFunc(GL_LESS);
    ss->glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    ss->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    shader_program_bind(&ss->shader);
    shader_uniform_set_mat4(&ss->cache, &ss->shader, "u_view", view.m, 0);
    shader_uniform_set_mat4(&ss->cache, &ss->shader, "u_projection", proj.m, 0);
    shader_uniform_set_vec3(&ss->cache, &ss->shader, "u_light_pos", light_pos);
    shader_uniform_set_float(&ss->cache, &ss->shader, "u_far", ss->far_plane);
    for (uint32_t i = 0; i < scene->count; ++i) {
        const render_renderable_t *r = &scene->items[i];
        if (r->mesh == NULL)
            continue;
        shader_uniform_set_mat4(&ss->cache, &ss->shader, "u_model", r->model, 0);
        static_mesh_bind(r->mesh);
        for (uint32_t s = 0; s < r->mesh->submesh_count; ++s)
            static_mesh_draw_submesh(r->mesh, s);
    }
    ss->glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void shadow_spot_bind(const shadow_spot_t *ss, shader_uniform_cache_t *cache,
                      const shader_program_t *program, uint32_t unit)
{
    if (ss == NULL || cache == NULL || program == NULL)
        return;
    ss->glActiveTexture(GL_TEXTURE0 + unit);
    ss->glBindTexture(GL_TEXTURE_2D, ss->map);
    shader_uniform_set_int(cache, program, "u_spot_map", (int32_t)unit);
    shader_uniform_set_mat4(cache, program, "u_spot_vp", ss->view_proj.m, 0);
    shader_uniform_set_float(cache, program, "u_spot_far", ss->far_plane);
}

void shadow_spot_destroy(shadow_spot_t *ss)
{
    if (ss == NULL)
        return;
    if (ss->glDeleteFramebuffers && ss->fbo)
        ss->glDeleteFramebuffers(1, &ss->fbo);
    if (ss->glDeleteRenderbuffers && ss->depth_rb)
        ss->glDeleteRenderbuffers(1, &ss->depth_rb);
    if (ss->glDeleteTextures && ss->map)
        ss->glDeleteTextures(1, &ss->map);
    shader_program_destroy(&ss->shader);
    ss->fbo = ss->depth_rb = ss->map = 0;
}
