/**
 * @file shadow_csm_render.c
 * @brief Static (cached) + dynamic (per-frame) cascade rendering (shadow_csm.h).
 */
#include "ferrum/renderer/shadow_csm.h"

#include <math.h>

#include "ferrum/renderer/gl_constants.h"
#include "ferrum/renderer/mesh/static_mesh.h"

/* Far-plane EVSM2 moments (d=1): w=exp(C), w^2=exp(2C). An empty texel must read
 * as the most distant occluder. These exceed [0,1], so they are cleared with the
 * unclamped glClearBufferfv (glClearColor would clamp to 1). C matches SC_FS. */
#define CSM_EVSM_C 30.0f

/* Render scene items [from, to) into `array`'s layers (one per cascade) using
 * each cascade's light matrix. The combined matrix is fed as u_projection with
 * u_view = identity (the VS multiplies u_projection*u_view*model). Every layer
 * is cleared to 1.0 (= far plane / "no occluder") before drawing. */
static void csm_draw_range(shadow_csm_t *csm, const render_scene_t *scene,
                           uint32_t from, uint32_t to, uint32_t array,
                           uint32_t res, uint32_t depth_rb)
{
    mat4_t identity = mat4_identity();
    float fw = expf(CSM_EVSM_C);
    float far_moments[2] = { fw, fw * fw };
    csm->glBindFramebuffer(GL_FRAMEBUFFER, csm->fbo);
    csm->glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                   GL_RENDERBUFFER, depth_rb);
    csm->glViewport(0, 0, (int32_t)res, (int32_t)res);
    /* Capture EVERY occluder: the room's surfaces are single-sided (normals face
     * inward), so from the sun's side outside their back faces would be culled --
     * leaving walls out of the map and the sun leaking through. Render both sides. */
    csm->glDisable(GL_CULL_FACE);
    csm->glEnable(GL_DEPTH_TEST);
    csm->glDepthFunc(GL_LESS);
    shader_program_bind(&csm->shader);
    shader_uniform_set_mat4(&csm->cache, &csm->shader, "u_view", identity.m, 0);

    for (uint32_t c = 0; c < csm->cascades; ++c) {
        csm->glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                       array, 0, (int32_t)c);
        /* Far state via unclamped clear; depth to 1. */
        csm->glClearBufferfv(GL_COLOR, 0, far_moments);
        csm->glClear(GL_DEPTH_BUFFER_BIT);
        shader_uniform_set_mat4(&csm->cache, &csm->shader, "u_projection",
                                csm->view_proj[c].m, 0);
        shader_uniform_set_vec3(&csm->cache, &csm->shader, "u_eye", csm->eye[c]);
        shader_uniform_set_float(&csm->cache, &csm->shader, "u_far",
                                 csm->far_plane[c]);
        for (uint32_t i = from; i < to; ++i) {
            const render_renderable_t *r = &scene->items[i];
            if (r->mesh == NULL)
                continue;
            shader_uniform_set_mat4(&csm->cache, &csm->shader, "u_model",
                                    r->model, 0);
            static_mesh_bind(r->mesh);
            for (uint32_t s = 0; s < r->mesh->submesh_count; ++s)
                static_mesh_draw_submesh(r->mesh, s);
        }
    }
    csm->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    /* Build the moment mip chain so the receiver can pick a soft filtered LOD. */
    csm->glBindTexture(GL_TEXTURE_2D_ARRAY, array);
    csm->glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
}

void shadow_csm_bake_static(shadow_csm_t *csm, const render_scene_t *scene)
{
    if (csm == NULL || scene == NULL || csm->static_valid)
        return;
    uint32_t to = scene->dynamic_from;
    if (to > scene->count)
        to = scene->count;
    csm_draw_range(csm, scene, 0u, to, csm->static_array, csm->static_res,
                   csm->depth_rb_static);
    csm->static_valid = true;
}

void shadow_csm_render_dynamic(shadow_csm_t *csm, const render_scene_t *scene)
{
    if (csm == NULL || scene == NULL)
        return;
    uint32_t from = scene->dynamic_from;
    if (from > scene->count)
        from = scene->count;
    /* Always runs (even with no dynamic casters) so the array is cleared to
     * "no occluder" and the shader's co-sample falls back to the static term. */
    csm_draw_range(csm, scene, from, scene->count, csm->dynamic_array,
                   csm->dynamic_res, csm->depth_rb_dynamic);
}
