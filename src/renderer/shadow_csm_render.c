/**
 * @file shadow_csm_render.c
 * @brief Static EVSM2 cascade bake (cached) + single dynamic distance map
 *        (per-frame) rendering (see shadow_csm.h).
 */
#include "ferrum/renderer/shadow_csm.h"

#include <math.h>

#include "ferrum/renderer/gl_constants.h"
#include "ferrum/renderer/mesh/static_mesh.h"

/* Far-plane EVSM2 moments (d=1): w=exp(C), w^2=exp(2C). An empty texel must read
 * as the most distant occluder. These exceed [0,1], so they are cleared with the
 * unclamped glClearBufferfv (glClearColor would clamp to 1). C matches SC_FS. */
#define CSM_EVSM_C 30.0f

/* Draw scene items [from, to) with the depth program bound; per-mesh u_model. */
static void csm_draw_items(shadow_csm_t *csm, const render_scene_t *scene,
                           uint32_t from, uint32_t to)
{
    for (uint32_t i = from; i < to; ++i) {
        const render_renderable_t *r = &scene->items[i];
        if (r->mesh == NULL)
            continue;
        shader_uniform_set_mat4(&csm->cache, &csm->shader, "u_model", r->model, 0);
        static_mesh_bind(r->mesh);
        for (uint32_t s = 0; s < r->mesh->submesh_count; ++s)
            static_mesh_draw_submesh(r->mesh, s);
    }
}

/* Common depth-pass GL state. Both sides are rendered (cull disabled) because the
 * room's single-sided surfaces would otherwise drop their sun-facing back faces
 * and leak light. u_view = identity; the light matrix is fed as u_projection. */
static void csm_begin_pass(shadow_csm_t *csm, uint32_t res, uint32_t depth_rb, int mode)
{
    mat4_t identity = mat4_identity();
    csm->glBindFramebuffer(GL_FRAMEBUFFER, csm->fbo);
    csm->glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                   GL_RENDERBUFFER, depth_rb);
    csm->glViewport(0, 0, (int32_t)res, (int32_t)res);
    csm->glDisable(GL_CULL_FACE);
    csm->glEnable(GL_DEPTH_TEST);
    csm->glDepthFunc(GL_LESS);
    shader_program_bind(&csm->shader);
    shader_uniform_set_mat4(&csm->cache, &csm->shader, "u_view", identity.m, 0);
    shader_uniform_set_int(&csm->cache, &csm->shader, "u_mode", mode);
}

void shadow_csm_bake_static(shadow_csm_t *csm, const render_scene_t *scene)
{
    if (csm == NULL || scene == NULL || csm->static_valid)
        return;
    uint32_t to = scene->dynamic_from;
    if (to > scene->count)
        to = scene->count;

    float fw = expf(CSM_EVSM_C);
    float far_moments[2] = { fw, fw * fw };
    csm_begin_pass(csm, csm->static_res, csm->depth_rb_static, 0 /* EVSM */);
    for (uint32_t c = 0; c < csm->cascades; ++c) {
        csm->glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                       csm->static_array, 0, (int32_t)c);
        csm->glClearBufferfv(GL_COLOR, 0, far_moments); /* far state (unclamped). */
        csm->glClear(GL_DEPTH_BUFFER_BIT);
        shader_uniform_set_mat4(&csm->cache, &csm->shader, "u_projection",
                                csm->view_proj[c].m, 0);
        shader_uniform_set_vec3(&csm->cache, &csm->shader, "u_eye", csm->eye[c]);
        shader_uniform_set_float(&csm->cache, &csm->shader, "u_far", csm->far_plane[c]);
        csm_draw_items(csm, scene, 0u, to);
    }
    csm->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    csm->static_valid = true;
}

void shadow_csm_render_dynamic(shadow_csm_t *csm, const render_scene_t *scene)
{
    if (csm == NULL || scene == NULL)
        return;
    uint32_t from = scene->dynamic_from;
    if (from > scene->count)
        from = scene->count;

    /* One orthographic distance face; plain depth for PCF at receive time.
     * Cleared to 1 (= far / no occluder) so an empty dynamic set leaves only the
     * static term after the co-sample. */
    csm_begin_pass(csm, csm->dynamic_res, csm->dyn_depth_rb, 1 /* distance */);
    csm->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                GL_TEXTURE_2D, csm->dyn_map, 0);
    csm->glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    csm->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    shader_uniform_set_mat4(&csm->cache, &csm->shader, "u_projection",
                            csm->dyn_view_proj.m, 0);
    shader_uniform_set_vec3(&csm->cache, &csm->shader, "u_eye", csm->dyn_eye);
    shader_uniform_set_float(&csm->cache, &csm->shader, "u_far", csm->dyn_far);
    csm_draw_items(csm, scene, from, scene->count);
    csm->glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
