/**
 * @file shadow_csm_render.c
 * @brief Static EVSM2 cascade bake (cached) + single dynamic distance map
 *        (per-frame) rendering (see shadow_csm.h).
 */
#include "ferrum/renderer/shadow_csm.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "ferrum/renderer/cull/frustum_cull.h"
#include "ferrum/renderer/gl_constants.h"
#include "ferrum/renderer/mesh/static_mesh.h"


/* Draw scene items [from, to) into the cascade whose light MVP is @p vp. When
 * @p cascade_filter >= 0 only casters CLASSIFIED into that cascade are drawn (so
 * each static cascade holds just its size tier); -1 draws all (the single
 * dynamic map). The light-frustum box cull is a secondary guard. */
static void csm_draw_items(shadow_csm_t *csm, const render_scene_t *scene,
                           uint32_t from, uint32_t to, const float vp[16],
                           int cascade_filter)
{
    float planes[6][4];
    frustum_extract_planes(vp, planes);
    uint32_t drawn = 0, total = 0;
    for (uint32_t i = from; i < to; ++i) {
        const render_renderable_t *r = &scene->items[i];
        if (r->mesh == NULL)
            continue;
        ++total;
        /* Light-frustum tiling: a caster draws into every tile its light-space XY
         * box overlaps (usually one, or two across a seam). (Dynamic pass passes
         * -1 -> no filter.) */
        if (cascade_filter >= 0 &&
            !shadow_csm_caster_in_cascade(csm, r, (uint32_t)cascade_filter))
            continue;
        if (frustum_cull_aabb(planes, r->model, r->mesh->aabb_min, r->mesh->aabb_max))
            continue;
        ++drawn;
        shader_uniform_set_mat4(&csm->cache, &csm->shader, "u_model", r->model, 0);
        static_mesh_bind(r->mesh);
        /* Per-submesh: with the mask active, TRANSLUCENT submeshes leave the
         * MAIN maps (light passes through; they shadow via the mask instead).
         * Mask off -> they cast ordinary hard shadows like everything else. */
        for (uint32_t s = 0; s < r->mesh->submesh_count; ++s) {
            if (csm->mask_enabled) {
                const render_material_t *m = render_submesh_material(scene, r, s);
                if (m != NULL && m->opacity < 0.999f)
                    continue;
            }
            static_mesh_draw_submesh(r->mesh, s);
        }
    }
    if (getenv("CSM_DEBUG"))
        fprintf(stderr, "  cascade draw: %u/%u meshes\n", drawn, total);
}

/* Common depth-pass draw state (no FBO bind -- the caller attaches its target).
 * Both sides are rendered (cull disabled) because the room's single-sided
 * surfaces would otherwise drop their sun-facing back faces and leak light.
 * u_view = identity; the light matrix is fed as u_projection. */
static void csm_begin_pass(shadow_csm_t *csm, uint32_t res, int mode)
{
    mat4_t identity = mat4_identity();
    csm->glViewport(0, 0, (int32_t)res, (int32_t)res);
    csm->glDisable(GL_CULL_FACE);
    csm->glEnable(GL_DEPTH_TEST);
    csm->glDepthFunc(GL_LESS);
    shader_program_bind(&csm->shader);
    shader_uniform_set_mat4(&csm->cache, &csm->shader, "u_view", identity.m, 0);
    shader_uniform_set_int(&csm->cache, &csm->shader, "u_mode", mode);
}

/* MSAA scratch for the one-shot static bake: a multisampled R32F color RBO +
 * depth RBO pair sized to one cascade. Rendered into per cascade, then
 * blit-resolved into the atlas layer. Returns 0 on any failure (caller falls
 * back to the single-sampled path). Out params receive the two RBO names. */
static uint32_t csm_msaa_scratch_create(shadow_csm_t *csm, uint32_t res,
                                        uint32_t samples, uint32_t color_fmt,
                                        uint32_t *out_color_rb,
                                        uint32_t *out_depth_rb)
{
    /* Sample-count ladder: a cascade-sized multisampled pair is GBs at high
     * res, so on OUT_OF_MEMORY / incomplete FBO halve the samples and retry
     * (8 -> 4 -> 2) before giving up. */
    for (uint32_t s = samples; s >= 2u; s >>= 1) {
        uint32_t fbo = 0, color_rb = 0, depth_rb = 0;
        while (csm->glGetError() != 0u) { } /* drain stale errors. */
        csm->glGenFramebuffers(1, &fbo);
        csm->glGenRenderbuffers(1, &color_rb);
        csm->glGenRenderbuffers(1, &depth_rb);
        csm->glBindRenderbuffer(GL_RENDERBUFFER, color_rb);
        csm->glRenderbufferStorageMultisample(GL_RENDERBUFFER, (int32_t)s,
                                              color_fmt, (int32_t)res,
                                              (int32_t)res);
        csm->glBindRenderbuffer(GL_RENDERBUFFER, depth_rb);
        csm->glRenderbufferStorageMultisample(GL_RENDERBUFFER, (int32_t)s,
                                              GL_DEPTH_COMPONENT24,
                                              (int32_t)res, (int32_t)res);
        csm->glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        csm->glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                       GL_RENDERBUFFER, color_rb);
        csm->glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                       GL_RENDERBUFFER, depth_rb);
        if (csm->glGetError() == 0u &&
            csm->glCheckFramebufferStatus(GL_FRAMEBUFFER) ==
                GL_FRAMEBUFFER_COMPLETE) {
            if (getenv("CSM_DEBUG"))
                fprintf(stderr, "csm bake: MSAA scratch %ux%u @ %ux\n",
                        res, res, s);
            *out_color_rb = color_rb;
            *out_depth_rb = depth_rb;
            return fbo;
        }
        csm->glBindFramebuffer(GL_FRAMEBUFFER, 0);
        csm->glDeleteFramebuffers(1, &fbo);
        csm->glDeleteRenderbuffers(1, &color_rb);
        csm->glDeleteRenderbuffers(1, &depth_rb);
    }
    return 0;
}

void shadow_csm_bake_static(shadow_csm_t *csm, const render_scene_t *scene)
{
    if (csm == NULL || scene == NULL || csm->static_valid || csm->static_base < 0)
        return;
    uint32_t to = scene->dynamic_from;
    if (to > scene->count)
        to = scene->count;

    /* Full-MSAA bake: render each cascade into a multisampled scratch target
     * and blit-resolve into its atlas layer; the resolve averages the caster
     * coverage per texel, antialiasing shadow silhouettes. One-shot cost. */
    uint32_t ms_fbo = 0, ms_color_rb = 0, ms_depth_rb = 0;
    if (csm->msaa > 1u)
        ms_fbo = csm_msaa_scratch_create(csm, csm->static_res, csm->msaa,
                                         GL_R32F, &ms_color_rb, &ms_depth_rb);

    float far_depth[4] = { 1.0f, 1.0f, 1.0f, 1.0f }; /* empty texel = farthest. */
    csm_begin_pass(csm, csm->static_res, 0);
    for (uint32_t c = 0; c < csm->cascades; ++c) {
        if (ms_fbo != 0)
            csm->glBindFramebuffer(GL_FRAMEBUFFER, ms_fbo);
        else
            /* Attach this cascade's slice of the resource-managed atlas. */
            shadow_atlas_bind_layer(&csm->static_atlas,
                                    (uint32_t)csm->static_base + c);
        csm->glClearBufferfv(GL_COLOR, 0, far_depth);
        csm->glClear(GL_DEPTH_BUFFER_BIT);
        shader_uniform_set_mat4(&csm->cache, &csm->shader, "u_projection",
                                csm->view_proj[c].m, 0);
        shader_uniform_set_vec3(&csm->cache, &csm->shader, "u_eye", csm->eye[c]);
        shader_uniform_set_float(&csm->cache, &csm->shader, "u_far", csm->far_plane[c]);
        csm_draw_items(csm, scene, 0u, to, csm->view_proj[c].m, (int)c);
        if (ms_fbo != 0) {
            /* Resolve: atlas layer as the draw target, scratch as read. The
             * final shadow_atlas_bind_layer leaves the atlas FBO bound on
             * GL_FRAMEBUFFER so the CSM_DUMP ReadPixels below still works. */
            shadow_atlas_bind_layer(&csm->static_atlas,
                                    (uint32_t)csm->static_base + c);
            csm->glBindFramebuffer(GL_READ_FRAMEBUFFER, ms_fbo);
            csm->glBlitFramebuffer(0, 0, (int32_t)csm->static_res,
                                   (int32_t)csm->static_res, 0, 0,
                                   (int32_t)csm->static_res,
                                   (int32_t)csm->static_res,
                                   GL_COLOR_BUFFER_BIT, GL_NEAREST);
            shadow_atlas_bind_layer(&csm->static_atlas,
                                    (uint32_t)csm->static_base + c);
        }

        /* CSM_DUMP: read this cascade's linear-depth map back and write it as a
         * grayscale PPM so the caster depth per cascade can be inspected. */
        if (getenv("CSM_DUMP")) {
            uint32_t res = csm->static_res;
            float *buf = malloc((size_t)res * res * sizeof(float));
            if (buf) {
                csm->glReadPixels(0, 0, (int32_t)res, (int32_t)res, GL_RED, GL_FLOAT, buf);
                char path[64]; snprintf(path, sizeof path, "csm_cascade_%u.pgm", c);
                FILE *fp = fopen(path, "wb");
                if (fp) {
                    fprintf(fp, "P5\n%u %u\n255\n", res, res);
                    /* glReadPixels rows are bottom-to-top; write top row first. */
                    for (int y = (int)res - 1; y >= 0; --y)
                    for (uint32_t x = 0; x < res; ++x) {
                        float d = buf[(uint32_t)y * res + x];
                        int v = (int)(d * 255.0f); v = v < 0 ? 0 : (v > 255 ? 255 : v);
                        unsigned char b = (unsigned char)v; fwrite(&b, 1, 1, fp);
                    }
                    fclose(fp);
                    fprintf(stderr, "CSM_DUMP: wrote %s (%ux%u)\n", path, res, res);
                }
                free(buf);
            }
        }
    }
    if (ms_fbo != 0) {
        csm->glDeleteFramebuffers(1, &ms_fbo);
        csm->glDeleteRenderbuffers(1, &ms_color_rb);
        csm->glDeleteRenderbuffers(1, &ms_depth_rb);
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
    csm->glBindFramebuffer(GL_FRAMEBUFFER, csm->fbo);
    csm->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                GL_TEXTURE_2D, csm->dyn_map, 0);
    csm->glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                   GL_RENDERBUFFER, csm->dyn_depth_rb);
    csm_begin_pass(csm, csm->dynamic_res, 1 /* distance */);
    csm->glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    csm->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    shader_uniform_set_mat4(&csm->cache, &csm->shader, "u_projection",
                            csm->dyn_view_proj.m, 0);
    shader_uniform_set_vec3(&csm->cache, &csm->shader, "u_eye", csm->dyn_eye);
    shader_uniform_set_float(&csm->cache, &csm->shader, "u_far", csm->dyn_far);
    csm_draw_items(csm, scene, from, scene->count, csm->dyn_view_proj.m, -1);
    csm->glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
