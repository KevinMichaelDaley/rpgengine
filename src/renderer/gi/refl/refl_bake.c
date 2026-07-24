/**
 * @file refl_bake.c
 * @brief Reflection-probe cube rasterization (see refl_bake.h): per-face
 *        render of the whole scene, lit with the sun + a hemispherical
 *        irradiance term + emissive, and float readback.
 */
#include "ferrum/renderer/gi/refl_bake.h"

#include <string.h>

#include "ferrum/math/mat4.h"
#include "ferrum/math/vec3.h"
#include "ferrum/renderer/gl_constants.h"
#include "ferrum/renderer/mesh/static_mesh.h"

/* Vertex: world position + normal through the face view-proj. */
static const char *const RB_VS =
    "#version 330 core\n"
    "layout(location=0) in vec3 in_position;\n"
    "layout(location=1) in vec3 in_normal;\n"
    "uniform mat4 u_model;\n"
    "uniform mat4 u_vp;\n"
    "out vec3 v_normal;\n"
    "void main(){\n"
    "  vec4 wp = u_model * vec4(in_position, 1.0);\n"
    "  v_normal = mat3(u_model) * in_normal;\n"
    "  gl_Position = u_vp * wp;\n"
    "}\n";
/* Fragment: the probe's IRRADIANCE term -- sun N.L direct + a hemispherical
 * sky/ground ambient (both cosine terms, so the stored cube is outgoing
 * radiance of a diffuse scene), plus emissive. Alpha stays 1 here; the
 * atlas alpha is overwritten by the SDF specular-occlusion pass. */
static const char *const RB_FS =
    "#version 330 core\n"
    "in vec3 v_normal;\n"
    "uniform vec3 u_sun_dir;\n"
    "uniform vec3 u_sun_color;\n"
    "uniform float u_sun_vis;\n"   /* SDF sun visibility AT the probe. */
    "uniform vec3 u_ambient;\n"
    "uniform vec3 u_tint;\n"
    "uniform vec3 u_emissive;\n"
    "layout(location=0) out vec4 o_color;\n"
    "void main(){\n"
    "  vec3 n = normalize(v_normal);\n"
    "  float ndl = max(dot(n, normalize(u_sun_dir)), 0.0);\n"
    "  vec3 irr = u_sun_color * (ndl * u_sun_vis)\n"
    "           + u_ambient * mix(0.55, 1.0, n.y * 0.5 + 0.5);\n"
    "  o_color = vec4(u_tint * irr + u_emissive, 1.0);\n"
    "}\n";

#define RB_LOAD(dst, name)                                                    \
    do {                                                                      \
        void *p_ = loader->get_proc_address((name), loader->user_data);       \
        if (p_ == NULL) return false;                                         \
        memcpy(&(dst), &p_, sizeof(p_));                                      \
    } while (0)

bool refl_bake_init(refl_bake_t *rb, const gl_loader_t *loader,
                    uint32_t face_res)
{
    if (rb == NULL || loader == NULL || loader->get_proc_address == NULL ||
        face_res == 0u)
        return false;
    memset(rb, 0, sizeof(*rb));
    rb->face_res = face_res;
    if (shader_program_create(&rb->shader, loader, RB_VS, RB_FS, NULL, 0) !=
        SHADER_PROGRAM_OK)
        return false;
    shader_uniform_cache_init(&rb->cache, &rb->shader);

    RB_LOAD(rb->glGenFramebuffers, "glGenFramebuffers");
    RB_LOAD(rb->glDeleteFramebuffers, "glDeleteFramebuffers");
    RB_LOAD(rb->glBindFramebuffer, "glBindFramebuffer");
    RB_LOAD(rb->glFramebufferTexture2D, "glFramebufferTexture2D");
    RB_LOAD(rb->glGenTextures, "glGenTextures");
    RB_LOAD(rb->glDeleteTextures, "glDeleteTextures");
    RB_LOAD(rb->glBindTexture, "glBindTexture");
    RB_LOAD(rb->glTexImage2D, "glTexImage2D");
    RB_LOAD(rb->glTexParameteri, "glTexParameteri");
    RB_LOAD(rb->glGenRenderbuffers, "glGenRenderbuffers");
    RB_LOAD(rb->glDeleteRenderbuffers, "glDeleteRenderbuffers");
    RB_LOAD(rb->glBindRenderbuffer, "glBindRenderbuffer");
    RB_LOAD(rb->glRenderbufferStorage, "glRenderbufferStorage");
    RB_LOAD(rb->glFramebufferRenderbuffer, "glFramebufferRenderbuffer");
    RB_LOAD(rb->glViewport, "glViewport");
    RB_LOAD(rb->glClearColor, "glClearColor");
    RB_LOAD(rb->glClear, "glClear");
    RB_LOAD(rb->glEnable, "glEnable");
    RB_LOAD(rb->glDisable, "glDisable");
    RB_LOAD(rb->glDepthFunc, "glDepthFunc");
    RB_LOAD(rb->glDrawElements, "glDrawElements");
    RB_LOAD(rb->glReadPixels, "glReadPixels");
    RB_LOAD(rb->glFinish, "glFinish");

    rb->glGenTextures(1, &rb->color);
    rb->glBindTexture(GL_TEXTURE_2D, rb->color);
    rb->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, (int32_t)face_res,
                     (int32_t)face_res, 0, GL_RGBA, GL_FLOAT, NULL);
    rb->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    rb->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    rb->glGenRenderbuffers(1, &rb->depth_rb);
    rb->glBindRenderbuffer(GL_RENDERBUFFER, rb->depth_rb);
    rb->glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24,
                              (int32_t)face_res, (int32_t)face_res);
    rb->glGenFramebuffers(1, &rb->fbo);
    rb->glBindFramebuffer(GL_FRAMEBUFFER, rb->fbo);
    rb->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, rb->color, 0);
    rb->glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                  GL_RENDERBUFFER, rb->depth_rb);
    rb->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

/* Cube-face view matrix matching shadow_cube (GL face conventions). */
static void rb_face_view(uint32_t face, const float p[3], mat4_t *out)
{
    static const float dir[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 },
                                     { 0, -1, 0 }, { 0, 0, 1 },
                                     { 0, 0, -1 } };
    static const float up[6][3] = { { 0, -1, 0 }, { 0, -1, 0 }, { 0, 0, 1 },
                                    { 0, 0, -1 }, { 0, -1, 0 },
                                    { 0, -1, 0 } };
    vec3_t eye = { p[0], p[1], p[2] };
    vec3_t tgt = { p[0] + dir[face][0], p[1] + dir[face][1],
                   p[2] + dir[face][2] };
    vec3_t u = { up[face][0], up[face][1], up[face][2] };
    mat4_look_at(eye, tgt, u, out);
}

void refl_bake_probe(refl_bake_t *rb, const render_scene_t *scene,
                     const float pos[3], const refl_bake_params_t *prm,
                     float sun_vis, float *faces[6])
{
    if (rb == NULL || scene == NULL || pos == NULL || prm == NULL ||
        faces == NULL)
        return;
    for (uint32_t f = 0; f < 6u; ++f)
        if (faces[f] == NULL)
            return;
    mat4_t proj;
    mat4_perspective(1.57079633f, 1.0f, 0.05f, 500.0f, &proj);

    rb->glBindFramebuffer(GL_FRAMEBUFFER, rb->fbo);
    rb->glViewport(0, 0, (int32_t)rb->face_res, (int32_t)rb->face_res);
    rb->glEnable(GL_DEPTH_TEST);
    rb->glDepthFunc(GL_LESS);
    shader_program_bind(&rb->shader);
    shader_uniform_set_vec3(&rb->cache, &rb->shader, "u_sun_dir",
                            prm->sun_dir);
    shader_uniform_set_vec3(&rb->cache, &rb->shader, "u_sun_color",
                            prm->sun_color);
    shader_uniform_set_float(&rb->cache, &rb->shader, "u_sun_vis", sun_vis);
    shader_uniform_set_vec3(&rb->cache, &rb->shader, "u_ambient",
                            prm->ambient);
    for (uint32_t f = 0; f < 6u; ++f) {
        mat4_t view, vp;
        rb_face_view(f, pos, &view);
        vp = mat4_mul(proj, view);
        shader_uniform_set_mat4(&rb->cache, &rb->shader, "u_vp", vp.m, 0);
        /* Unwritten texels = sky: store the ambient as escape radiance. */
        rb->glClearColor(prm->ambient[0], prm->ambient[1], prm->ambient[2],
                         1.0f);
        rb->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        for (uint32_t i = 0; i < scene->count; ++i) {
            const render_renderable_t *r = &scene->items[i];
            if (r->mesh == NULL)
                continue;
            shader_uniform_set_mat4(&rb->cache, &rb->shader, "u_model",
                                    r->model, 0);
            static_mesh_bind(r->mesh);
            for (uint32_t s = 0; s < r->mesh->submesh_count; ++s) {
                const render_submesh_t *sub = &r->mesh->submeshes[s];
                const render_material_t *mat = r->material;
                if (mat == NULL && scene->materials != NULL &&
                    sub->material_slot < scene->material_count)
                    mat = &scene->materials[sub->material_slot];
                float tint[3] = { 0.6f, 0.6f, 0.6f };
                float emi[3] = { 0.0f, 0.0f, 0.0f };
                if (mat != NULL) {
                    tint[0] = mat->tint[0];
                    tint[1] = mat->tint[1];
                    tint[2] = mat->tint[2];
                    emi[0] = mat->emissive_color[0] * mat->emissive_strength;
                    emi[1] = mat->emissive_color[1] * mat->emissive_strength;
                    emi[2] = mat->emissive_color[2] * mat->emissive_strength;
                }
                shader_uniform_set_vec3(&rb->cache, &rb->shader, "u_tint",
                                        tint);
                shader_uniform_set_vec3(&rb->cache, &rb->shader,
                                        "u_emissive", emi);
                size_t off = (size_t)sub->index_offset * sizeof(uint32_t);
                rb->glDrawElements(GL_TRIANGLES, (int32_t)sub->index_count,
                                   GL_UNSIGNED_INT, (const void *)off);
            }
        }
        rb->glFinish();
        rb->glReadPixels(0, 0, (int32_t)rb->face_res, (int32_t)rb->face_res,
                         GL_RGBA, GL_FLOAT, faces[f]);
    }
    rb->glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void refl_bake_destroy(refl_bake_t *rb)
{
    if (rb == NULL)
        return;
    if (rb->glDeleteFramebuffers && rb->fbo)
        rb->glDeleteFramebuffers(1, &rb->fbo);
    if (rb->glDeleteRenderbuffers && rb->depth_rb)
        rb->glDeleteRenderbuffers(1, &rb->depth_rb);
    if (rb->glDeleteTextures && rb->color)
        rb->glDeleteTextures(1, &rb->color);
    shader_program_destroy(&rb->shader);
    rb->fbo = rb->depth_rb = rb->color = 0;
}
