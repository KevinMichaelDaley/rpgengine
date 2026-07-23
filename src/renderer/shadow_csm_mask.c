/**
 * @file shadow_csm_mask.c
 * @brief CSM translucency mask (rpg-29zj): translucent casters (material
 *        opacity < 1) leave the MAIN shadow maps -- sunlight passes through
 *        them -- and render here instead, into a per-cascade MRT pair:
 *
 *          color atlas  RGBA16F  (rgb = transmission tint, a = coverage)
 *          depth atlas  R32F     (linear distance from the cascade eye / far)
 *
 *        depth-tested so the NEAREST translucent surface per light texel wins,
 *        plus a low-res 2D pair for dynamic translucent casters. At receive
 *        time (pbr_shader ::pbr_csm_translucency) a fragment lying BEYOND the
 *        mask distance -- behind the glass as seen from the light -- has its
 *        sun term multiplied by the mask tint: exactly "can the light see me
 *        through this translucent surface". The caustics compute (rpg-kbqd)
 *        consumes the same pair.
 *
 * Ownership: the mask atlases live in the csm's existing registry; the shader,
 * FBO and dynamic pair are owned here and torn down by shadow_csm_destroy.
 */
#include "ferrum/renderer/shadow_csm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/renderer/cull/frustum_cull.h"
#include "ferrum/renderer/gl_constants.h"
#include "ferrum/renderer/material.h"
#include "ferrum/renderer/mesh/static_mesh.h"

/* Same world-position pass-through as the main caster VS. */
static const char *const SM_VS =
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
/* MRT: attachment 0 = tint+coverage, attachment 1 = normalised distance.
 * Depth-tested (shared renderbuffer) so the nearest translucent layer wins. */
static const char *const SM_FS =
    "#version 330 core\n"
    "in vec3 v_world;\n"
    "uniform vec3 u_eye;\n"
    "uniform float u_far;\n"
    "uniform vec3 u_tint;\n"
    "uniform float u_alpha;\n"
    "layout(location=0) out vec4 o_color;\n"
    "layout(location=1) out float o_depth;\n"
    "void main(){\n"
    "  o_color = vec4(u_tint, u_alpha);\n"
    "  o_depth = clamp(distance(v_world, u_eye) / u_far, 0.0, 1.0);\n"
    "}\n";

/* Draw the TRANSLUCENT casters of [from, to) with the mask program bound.
 * Mirrors csm_draw_items (shadow_csm_render.c) but filters by material and
 * uploads the per-material transmission tint + coverage. */
static void mask_draw_items(shadow_csm_t *csm, const render_scene_t *scene,
                            uint32_t from, uint32_t to, const float vp[16],
                            int cascade_filter)
{
    float planes[6][4];
    frustum_extract_planes(vp, planes);
    for (uint32_t i = from; i < to; ++i) {
        const render_renderable_t *r = &scene->items[i];
        if (r->mesh == NULL)
            continue;
        if (cascade_filter >= 0 &&
            !shadow_csm_caster_in_cascade(csm, r, (uint32_t)cascade_filter))
            continue;
        if (frustum_cull_aabb(planes, r->model, r->mesh->aabb_min,
                              r->mesh->aabb_max))
            continue;
        static_mesh_bind(r->mesh);
        shader_uniform_set_mat4(&csm->mask_cache, &csm->mask_shader, "u_model",
                                r->model, 0);
        /* Per-submesh: only the TRANSLUCENT submeshes cast into the mask, each
         * with its own transmission tint + coverage. */
        for (uint32_t s = 0; s < r->mesh->submesh_count; ++s) {
            const render_material_t *m = render_submesh_material(scene, r, s);
            if (m == NULL || m->opacity >= 0.999f)
                continue;
            shader_uniform_set_vec3(&csm->mask_cache, &csm->mask_shader, "u_tint",
                                    m->tint);
            shader_uniform_set_float(&csm->mask_cache, &csm->mask_shader,
                                     "u_alpha", 1.0f - m->opacity);
            static_mesh_draw_submesh(r->mesh, s);
        }
    }
}

/* Shared pass state + per-target clear: color (0,0,0,0) = "no translucent
 * caster here", distance 1 = farthest. */
static void mask_begin_target(shadow_csm_t *csm, uint32_t color_tex,
                              int32_t color_layer, uint32_t depth_tex,
                              int32_t depth_layer, uint32_t depth_rb,
                              uint32_t res)
{
    static const uint32_t bufs[2] = { GL_COLOR_ATTACHMENT0,
                                      GL_COLOR_ATTACHMENT0 + 1u };
    static const float clear_color[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    static const float clear_far[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    csm->glBindFramebuffer(GL_FRAMEBUFFER, csm->mask_fbo);
    if (color_layer >= 0) {
        csm->glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                       color_tex, 0, color_layer);
        csm->glFramebufferTextureLayer(GL_FRAMEBUFFER,
                                       GL_COLOR_ATTACHMENT0 + 1u, depth_tex, 0,
                                       depth_layer);
    } else {
        csm->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                    GL_TEXTURE_2D, color_tex, 0);
        csm->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + 1u,
                                    GL_TEXTURE_2D, depth_tex, 0);
    }
    csm->glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                   GL_RENDERBUFFER, depth_rb);
    csm->glDrawBuffers(2, bufs);
    csm->glViewport(0, 0, (int32_t)res, (int32_t)res);
    csm->glDisable(GL_CULL_FACE);
    csm->glEnable(GL_DEPTH_TEST);
    csm->glDepthFunc(GL_LESS);
    csm->glClearBufferfv(GL_COLOR, 0, clear_color);
    csm->glClearBufferfv(GL_COLOR, 1, clear_far);
    csm->glClear(GL_DEPTH_BUFFER_BIT);
}

/* Make one 2D target of the dynamic mask pair. COLOR is bilinear -- the
 * transmission tint x coverage is a filterable field once the receiver gates
 * depth softly (see pbr_csm_glass) -- while DEPTH stays point-sampled: lerped
 * silhouette distances would invent phantom mid-depths. */
static uint32_t mask_make_2d(shadow_csm_t *csm, uint32_t internal_format,
                             uint32_t format, uint32_t res, int32_t filt)
{
    uint32_t tex = 0;
    csm->glGenTextures(1, &tex);
    csm->glBindTexture(GL_TEXTURE_2D, tex);
    csm->glTexImage2D(GL_TEXTURE_2D, 0, (int32_t)internal_format, (int32_t)res,
                      (int32_t)res, 0, format, GL_FLOAT, NULL);
    csm->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filt);
    csm->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filt);
    csm->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    csm->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return tex;
}

bool shadow_csm_mask_init(shadow_csm_t *csm, const gl_loader_t *loader)
{
    if (csm == NULL || loader == NULL || loader->get_proc_address == NULL ||
        csm->cascades == 0u)
        return false;
    /* glDrawBuffers is the one entry point the main CSM never needed. */
    void *p = loader->get_proc_address("glDrawBuffers", loader->user_data);
    if (p == NULL)
        return false;
    memcpy(&csm->glDrawBuffers, &p, sizeof p);

    if (shader_program_create(&csm->mask_shader, loader, SM_VS, SM_FS, NULL,
                              0) != SHADER_PROGRAM_OK)
        return false;
    shader_uniform_cache_init(&csm->mask_cache, &csm->mask_shader);

    /* The mask may run at its own (typically higher) resolution: glass
     * silhouettes are what the eye reads at translucency boundaries, so extra
     * texels there pay off more than in the opaque maps. Receivers sample by
     * normalized uv, so no receiver-side changes are needed. */
    uint32_t mres = csm->mask_res ? csm->mask_res : csm->static_res;
    shadow_atlas_config_t color_cfg = {
        .loader = loader,
        .registry = &csm->registry,
        .resolution = mres,
        .layers = csm->cascades,
        .internal_format = GL_RGBA16F,
        /* BILINEAR: with the receiver's soft depth gate (pbr_csm_glass) the
         * tint x coverage field is filterable, and edge bleed becomes the glass
         * shadow's own penumbra instead of per-tap binary speckle. (The old
         * point-sampled + hard-gate combo speckled every translucency boundary.) */
        .nearest = false,
    };
    shadow_atlas_config_t depth_cfg = color_cfg;
    depth_cfg.internal_format = GL_R32F;
    depth_cfg.nearest = true;   /* glass distance: lerped silhouettes lie. */
    if (!shadow_atlas_init(&csm->mask_color_atlas, &color_cfg) ||
        !shadow_atlas_init(&csm->mask_depth_atlas, &depth_cfg))
        return false;
    csm->mask_color_base = shadow_atlas_alloc(&csm->mask_color_atlas,
                                              csm->cascades);
    csm->mask_depth_base = shadow_atlas_alloc(&csm->mask_depth_atlas,
                                              csm->cascades);
    if (csm->mask_color_base < 0 || csm->mask_depth_base < 0)
        return false;

    csm->glGenFramebuffers(1, &csm->mask_fbo);
    csm->glGenRenderbuffers(1, &csm->mask_depth_rb);
    csm->glBindRenderbuffer(GL_RENDERBUFFER, csm->mask_depth_rb);
    csm->glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24,
                               (int32_t)mres, (int32_t)mres);
    csm->dyn_mask_color = mask_make_2d(csm, GL_RGBA16F, GL_RGBA,
                                       csm->dynamic_res,
                                       (int32_t)GL_LINEAR);
    csm->dyn_mask_depth = mask_make_2d(csm, GL_R32F, GL_RED,
                                       csm->dynamic_res,
                                       (int32_t)GL_NEAREST);
    csm->glGenRenderbuffers(1, &csm->dyn_mask_depth_rb);
    csm->glBindRenderbuffer(GL_RENDERBUFFER, csm->dyn_mask_depth_rb);
    csm->glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24,
                               (int32_t)csm->dynamic_res,
                               (int32_t)csm->dynamic_res);
    csm->mask_static_valid = false;
    csm->mask_enabled = true;
    return true;
}

/* One multisampled RBO of @p samples at @p res x @p res. */
static uint32_t mask_ms_rb(shadow_csm_t *csm, uint32_t internal_format,
                           uint32_t res, uint32_t samples)
{
    uint32_t rb = 0;
    csm->glGenRenderbuffers(1, &rb);
    csm->glBindRenderbuffer(GL_RENDERBUFFER, rb);
    csm->glRenderbufferStorageMultisample(GL_RENDERBUFFER, (int32_t)samples,
                                          internal_format, (int32_t)res,
                                          (int32_t)res);
    return rb;
}

/* Blit-resolve one multisampled scratch attachment into one atlas layer.
 * The mask FBO doubles as the single-attachment draw target: the destination
 * layer goes on ATTACHMENT0 and DrawBuffers is narrowed to it. */
static void mask_ms_resolve(shadow_csm_t *csm, uint32_t ms_fbo,
                            uint32_t src_attachment, uint32_t dst_tex,
                            int32_t dst_layer, uint32_t res)
{
    static const uint32_t one_buf[1] = { GL_COLOR_ATTACHMENT0 };
    csm->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, csm->mask_fbo);
    csm->glFramebufferTextureLayer(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                   dst_tex, 0, dst_layer);
    /* Drop the stale ATTACHMENT1 layer so mixed formats can't fail
     * completeness while DrawBuffers is narrowed to attachment 0. */
    csm->glFramebufferTextureLayer(GL_DRAW_FRAMEBUFFER,
                                   GL_COLOR_ATTACHMENT0 + 1u, 0, 0, 0);
    csm->glDrawBuffers(1, one_buf);
    csm->glBindFramebuffer(GL_READ_FRAMEBUFFER, ms_fbo);
    csm->glReadBuffer(src_attachment);
    csm->glBlitFramebuffer(0, 0, (int32_t)res, (int32_t)res, 0, 0,
                           (int32_t)res, (int32_t)res, GL_COLOR_BUFFER_BIT,
                           GL_NEAREST);
}

void shadow_csm_mask_bake_static(shadow_csm_t *csm,
                                 const render_scene_t *scene)
{
    if (csm == NULL || scene == NULL || !csm->mask_enabled ||
        csm->mask_static_valid)
        return;
    uint32_t to = scene->dynamic_from;
    if (to > scene->count)
        to = scene->count;
    uint32_t mres = csm->mask_res ? csm->mask_res : csm->static_res;

    /* Full-MSAA bake (matching the opaque cascade bake): draw the translucent
     * casters into a multisampled MRT scratch, then resolve tint+coverage and
     * distance separately into their atlas layers. The averaged coverage along
     * glass silhouettes is what removes the jagged translucency edges. */
    uint32_t ms_fbo = 0, ms_color_rb = 0, ms_dist_rb = 0, ms_depth_rb = 0;
    /* Sample-count ladder (as in the opaque cascade bake): the MRT scratch is
     * GBs at high res, so on OUT_OF_MEMORY / incomplete FBO halve the samples
     * and retry before falling back to the single-sampled path. */
    for (uint32_t s = csm->msaa; ms_fbo == 0 && s >= 2u; s >>= 1) {
        static const uint32_t bufs[2] = { GL_COLOR_ATTACHMENT0,
                                          GL_COLOR_ATTACHMENT0 + 1u };
        while (csm->glGetError() != 0u) { } /* drain stale errors. */
        ms_color_rb = mask_ms_rb(csm, GL_RGBA16F, mres, s);
        ms_dist_rb = mask_ms_rb(csm, GL_R32F, mres, s);
        ms_depth_rb = mask_ms_rb(csm, GL_DEPTH_COMPONENT24, mres, s);
        csm->glGenFramebuffers(1, &ms_fbo);
        csm->glBindFramebuffer(GL_FRAMEBUFFER, ms_fbo);
        csm->glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                       GL_RENDERBUFFER, ms_color_rb);
        csm->glFramebufferRenderbuffer(GL_FRAMEBUFFER,
                                       GL_COLOR_ATTACHMENT0 + 1u,
                                       GL_RENDERBUFFER, ms_dist_rb);
        csm->glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                       GL_RENDERBUFFER, ms_depth_rb);
        csm->glDrawBuffers(2, bufs);
        if (getenv("CSM_DEBUG") &&
            csm->glCheckFramebufferStatus(GL_FRAMEBUFFER) ==
                GL_FRAMEBUFFER_COMPLETE)
            fprintf(stderr, "csm mask bake: MSAA scratch %ux%u @ %ux\n", mres,
                    mres, s);
        if (csm->glGetError() != 0u ||
            csm->glCheckFramebufferStatus(GL_FRAMEBUFFER) !=
                GL_FRAMEBUFFER_COMPLETE) {
            csm->glBindFramebuffer(GL_FRAMEBUFFER, 0);
            csm->glDeleteFramebuffers(1, &ms_fbo);
            csm->glDeleteRenderbuffers(1, &ms_color_rb);
            csm->glDeleteRenderbuffers(1, &ms_dist_rb);
            csm->glDeleteRenderbuffers(1, &ms_depth_rb);
            ms_fbo = 0; /* halve and retry (or fall back single-sampled). */
        }
    }

    mat4_t identity = mat4_identity();
    shader_program_bind(&csm->mask_shader);
    shader_uniform_set_mat4(&csm->mask_cache, &csm->mask_shader, "u_view",
                            identity.m, 0);
    for (uint32_t c = 0; c < csm->cascades; ++c) {
        if (ms_fbo != 0) {
            /* Scratch target: same pass state + clears as mask_begin_target,
             * but the attachments are the persistent multisampled RBOs. */
            static const uint32_t bufs[2] = { GL_COLOR_ATTACHMENT0,
                                              GL_COLOR_ATTACHMENT0 + 1u };
            static const float clear_color[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            /* Distance clears to 0 (NOT the usual far=1): the resolve blit
             * AVERAGES samples, so a silhouette texel mixes the glass distance
             * with the clear. Mixing toward far would push the stored distance
             * beyond receivers just behind the glass, closing the receiver's
             * depth gate and eroding the antialiased edge; mixing toward 0
             * keeps the gate open so the resolved partial coverage fades the
             * glass shadow out smoothly. Empty texels are harmless at 0: every
             * consumer gates on coverage first. */
            static const float clear_near[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            csm->glBindFramebuffer(GL_FRAMEBUFFER, ms_fbo);
            csm->glDrawBuffers(2, bufs);
            csm->glViewport(0, 0, (int32_t)mres, (int32_t)mres);
            csm->glDisable(GL_CULL_FACE);
            csm->glEnable(GL_DEPTH_TEST);
            csm->glDepthFunc(GL_LESS);
            csm->glClearBufferfv(GL_COLOR, 0, clear_color);
            csm->glClearBufferfv(GL_COLOR, 1, clear_near);
            csm->glClear(GL_DEPTH_BUFFER_BIT);
        } else {
            mask_begin_target(csm, csm->mask_color_atlas.texture,
                              csm->mask_color_base + (int32_t)c,
                              csm->mask_depth_atlas.texture,
                              csm->mask_depth_base + (int32_t)c,
                              csm->mask_depth_rb, mres);
        }
        shader_uniform_set_mat4(&csm->mask_cache, &csm->mask_shader,
                                "u_projection", csm->view_proj[c].m, 0);
        shader_uniform_set_vec3(&csm->mask_cache, &csm->mask_shader, "u_eye",
                                csm->eye[c]);
        shader_uniform_set_float(&csm->mask_cache, &csm->mask_shader, "u_far",
                                 csm->far_plane[c]);
        mask_draw_items(csm, scene, 0u, to, csm->view_proj[c].m, (int)c);
        if (ms_fbo != 0) {
            mask_ms_resolve(csm, ms_fbo, GL_COLOR_ATTACHMENT0,
                            csm->mask_color_atlas.texture,
                            csm->mask_color_base + (int32_t)c, mres);
            mask_ms_resolve(csm, ms_fbo, GL_COLOR_ATTACHMENT0 + 1u,
                            csm->mask_depth_atlas.texture,
                            csm->mask_depth_base + (int32_t)c, mres);
        }
    }
    if (ms_fbo != 0) {
        csm->glDeleteFramebuffers(1, &ms_fbo);
        csm->glDeleteRenderbuffers(1, &ms_color_rb);
        csm->glDeleteRenderbuffers(1, &ms_dist_rb);
        csm->glDeleteRenderbuffers(1, &ms_depth_rb);
    }
    csm->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    csm->mask_static_valid = true;
}

void shadow_csm_mask_render_dynamic(shadow_csm_t *csm,
                                    const render_scene_t *scene)
{
    if (csm == NULL || scene == NULL || !csm->mask_enabled)
        return;
    uint32_t from = scene->dynamic_from;
    if (from > scene->count)
        from = scene->count;
    mat4_t identity = mat4_identity();
    shader_program_bind(&csm->mask_shader);
    shader_uniform_set_mat4(&csm->mask_cache, &csm->mask_shader, "u_view",
                            identity.m, 0);
    mask_begin_target(csm, csm->dyn_mask_color, -1, csm->dyn_mask_depth, -1,
                      csm->dyn_mask_depth_rb, csm->dynamic_res);
    shader_uniform_set_mat4(&csm->mask_cache, &csm->mask_shader,
                            "u_projection", csm->dyn_view_proj.m, 0);
    shader_uniform_set_vec3(&csm->mask_cache, &csm->mask_shader, "u_eye",
                            csm->dyn_eye);
    shader_uniform_set_float(&csm->mask_cache, &csm->mask_shader, "u_far",
                             csm->dyn_far);
    mask_draw_items(csm, scene, from, scene->count, csm->dyn_view_proj.m, -1);
    csm->glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void shadow_csm_mask_bind(const shadow_csm_t *csm,
                          shader_uniform_cache_t *cache,
                          const shader_program_t *program,
                          uint32_t unit_color, uint32_t unit_depth,
                          uint32_t unit_dyn_color, uint32_t unit_dyn_depth)
{
    /* Distinct units even when disabled: sampler types must not alias. */
    shader_uniform_set_int(cache, program, "u_csm_mask_color",
                           (int32_t)unit_color);
    shader_uniform_set_int(cache, program, "u_csm_mask_depth",
                           (int32_t)unit_depth);
    shader_uniform_set_int(cache, program, "u_dyn_mask_color",
                           (int32_t)unit_dyn_color);
    shader_uniform_set_int(cache, program, "u_dyn_mask_depth",
                           (int32_t)unit_dyn_depth);
    shader_uniform_set_int(cache, program, "u_csm_mask_on",
                           (csm != NULL && csm->mask_enabled) ? 1 : 0);
    if (csm == NULL || !csm->mask_enabled)
        return;
    csm->glActiveTexture(GL_TEXTURE0 + unit_color);
    csm->glBindTexture(GL_TEXTURE_2D_ARRAY, csm->mask_color_atlas.texture);
    csm->glActiveTexture(GL_TEXTURE0 + unit_depth);
    csm->glBindTexture(GL_TEXTURE_2D_ARRAY, csm->mask_depth_atlas.texture);
    csm->glActiveTexture(GL_TEXTURE0 + unit_dyn_color);
    csm->glBindTexture(GL_TEXTURE_2D, csm->dyn_mask_color);
    csm->glActiveTexture(GL_TEXTURE0 + unit_dyn_depth);
    csm->glBindTexture(GL_TEXTURE_2D, csm->dyn_mask_depth);
    csm->glActiveTexture(GL_TEXTURE0);
}
