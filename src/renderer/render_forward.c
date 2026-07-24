/**
 * @file render_forward.c
 * @brief Clustered forward+ pipeline driver (see render_forward.h).
 */
#include "ferrum/renderer/render_forward.h"

#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "ferrum/renderer/gl_constants.h"
#ifndef GL_ONE
#define GL_ONE 1  /* additive-blend factor for the overdraw pass. */
#endif
#include "ferrum/renderer/cull/frustum_cull.h"
#include "ferrum/renderer/material.h"
#include "ferrum/renderer/mesh/static_mesh.h"
#include "ferrum/renderer/pbr_shader.h"

#ifdef TRACY_ENABLE
#include "tracy/TracyC.h"
#define FR_ZONE(v, name) TracyCZoneN(v, name, true)
#define FR_ZONE_END(v) TracyCZoneEnd(v)
#else
#define FR_ZONE(v, name)
#define FR_ZONE_END(v)
#endif

/* ── Pass callbacks (driven from the graph nodes; user_data == driver) ── */

/* Depth pre-pass: colour-masked depth-only draw of every renderable. */
static void fwd_depth_submit(void *ud)
{
    render_forward_t *f = (render_forward_t *)ud;
    if (f->scene != NULL && !f->no_prepass)   /* PBR_NOPREPASS: A/B the early-Z. */
        depth_prepass_execute(&f->depth, f->scene, f->cfg.draw_distance);
}

/* Light cull: assign the scene's lights to the froxel clusters for the camera,
 * pack them, and upload the grid + light data to the forward+ buffers. */
static void fwd_cull_submit(void *ud)
{
    render_forward_t *f = (render_forward_t *)ud;
    const render_scene_t *s = f->scene;
    if (s == NULL)
        return;
    uint32_t n = 0;
    const render_light_t *lights = NULL;
    if (s->lights != NULL) {
        n = s->lights->count;
        if (n > f->cfg.max_lights)
            n = f->cfg.max_lights;
        lights = s->lights->lights;
    }
    cluster_grid_build(&f->clusters, &s->camera, lights, n);
    for (uint32_t i = 0; i < n; ++i) {
        forward_plus_pack_light(&lights[i], &f->light_data[i * 16]);
        /* t3.y = this light's cube-array shadow slot (-1 = none), assigned in
         * render_forward_render before the graph runs. */
        f->light_data[i * 16 + 13] = (float)(f->shadow_slot ? f->shadow_slot[i] : -1);
    }
    forward_plus_upload(&f->fp, &f->clusters, f->light_data, n);
}

/* Forward+ shading: bind the PBR program + cluster buffers, then draw every
 * renderable. The clustered path loops the per-cluster light list in-shader
 * (u_clustered=1 from forward_plus_bind), so u_light_count stays 0. */
static void fwd_forward_submit(void *ud)
{
    render_forward_t *f = (render_forward_t *)ud;
    const render_scene_t *s = f->scene;
    if (s == NULL)
        return;

    /* LEQUAL + depth writes work whether or not the depth pre-pass ran (the
     * pre-pass writes exact depth with LESS; without it the buffer is cleared). */
    f->depth.glEnable(GL_DEPTH_TEST);
    f->depth.glDepthFunc(GL_LEQUAL);
    f->depth.glDepthMask(1);
    /* Overdraw heatmap: pass EVERY fragment (GL_ALWAYS, no depth write) and
     * ADDITIVELY blend the debug-mode-11 constant, so the framebuffer sums one
     * increment per shaded fragment. */
    if (f->overdraw && f->glBlendFunc != NULL) {
        f->depth.glDepthFunc(GL_ALWAYS);
        f->depth.glDepthMask(0);
        f->depth.glEnable(GL_BLEND);
        f->glBlendFunc(GL_ONE, GL_ONE);
    }

    shader_program_bind(&f->pbr);
    shader_uniform_set_mat4(&f->cache, &f->pbr, "u_view", s->camera.view, 0);
    shader_uniform_set_mat4(&f->cache, &f->pbr, "u_projection", s->camera.proj, 0);
    shader_uniform_set_vec3(&f->cache, &f->pbr, "u_eye_pos", s->camera.eye);
    shader_uniform_set_vec3(&f->cache, &f->pbr, "u_sun_dir", f->cfg.sun_dir);
    shader_uniform_set_vec3(&f->cache, &f->pbr, "u_sun_color", f->cfg.sun_color);
    shader_uniform_set_vec3(&f->cache, &f->pbr, "u_ambient", f->cfg.ambient);
    shader_uniform_set_int(&f->cache, &f->pbr, "u_light_count", 0);
    { const char *dbg = getenv("PBR_DEBUG");
      int mode = f->overdraw ? 11 : (dbg ? atoi(dbg) : 0);   /* 11 = overdraw. */
      shader_uniform_set_int(&f->cache, &f->pbr, "u_debug_mode", mode); }
    forward_plus_bind(&f->fp, &f->cache, &f->pbr, &f->cfg.cluster,
                      f->cfg.screen_w, f->cfg.screen_h);

    /* Optional baked SH lightmap on units 7..15 (material uses 0..6, forward+
     * 16..19). Combines static GI (SH) with the clustered dynamic lights. */
    /* ALWAYS bind the SH array samplers to units 7..15 and point the uniforms
     * there, even when disabled: otherwise u_sh0..8 (sampler2DArray) default to
     * unit 0, which holds the albedo (sampler2D) -> a type conflict that makes
     * every draw INVALID_OPERATION and corrupts the whole pass. u_sh_enabled
     * alone gates the sampling; a 0 texture here is just incomplete (reads black). */
    {
        static const char *const shn[9] = { "u_sh0", "u_sh1", "u_sh2", "u_sh3",
                                            "u_sh4", "u_sh5", "u_sh6", "u_sh7",
                                            "u_sh8" };
        for (int c = 0; c < 9; ++c) {
            f->fp.glActiveTexture(GL_TEXTURE0 + 7u + (uint32_t)c);
            /* Per-chunk lightmaps (rpg-yfa4): the SH coeff atlases are a
             * TEXTURE_2D_ARRAY (one layer per chunk); each mesh samples its own
             * layer via u_sh_layer. Single-atlas bakes upload a 1-layer array. */
            f->fp.glBindTexture(GL_TEXTURE_2D_ARRAY, f->cfg.sh_tex[c]);
            shader_uniform_set_int(&f->cache, &f->pbr, shn[c], 7 + c);
        }
        shader_uniform_set_int(&f->cache, &f->pbr, "u_sh_enabled", f->cfg.sh_enabled ? 1 : 0);
        shader_uniform_set_float(&f->cache, &f->pbr, "u_sh_scale",
                                 f->cfg.sh_scale > 0.0f ? f->cfg.sh_scale : 1.0f);
        /* 0 (unset by other callers) -> 1.0: full mapped normal = old behavior. */
        shader_uniform_set_float(&f->cache, &f->pbr, "u_sh_normal_bias",
                                 f->cfg.sh_normal_bias > 0.0f ? f->cfg.sh_normal_bias : 1.0f);
    }

    /* Optional point-light cube shadow on unit 20 (the movable light whose flat
     * index is shadow_light). Rendered before this pass in render_forward_render. */
    if (f->cfg.shadow_res > 0u) {
        /* Cube-map ARRAY on unit 20; each light's slot rides in its packed data. */
        shadow_cube_bind(&f->shadow, &f->cache, &f->pbr, 20u);
        shader_uniform_set_float(&f->cache, &f->pbr, "u_shadow_bias", f->cfg.shadow_bias);
    } else {
        shader_uniform_set_int(&f->cache, &f->pbr, "u_shadow_cube_arr", 20);
    }
    /* Spot 2D shadow on unit 21. */
    if (f->cfg.spot_light >= 0 && f->cfg.spot_res > 0u) {
        shadow_spot_bind(&f->spot, &f->cache, &f->pbr, 21u);
        shader_uniform_set_int(&f->cache, &f->pbr, "u_spot_light", f->cfg.spot_light);
        shader_uniform_set_float(&f->cache, &f->pbr, "u_spot_bias", f->cfg.spot_bias);
    } else {
        shader_uniform_set_int(&f->cache, &f->pbr, "u_spot_light", -1);
    }
    /* Cascaded directional (sun) shadow: static array on unit 22, dynamic on 23. */
    if (f->cfg.dir_cascades > 0u) {
        shadow_csm_bind(&f->csm, &f->cache, &f->pbr, 22u, 23u);
        shader_uniform_set_float(&f->cache, &f->pbr, "u_dir_bias", f->cfg.dir_bias);
        shader_uniform_set_int(&f->cache, &f->pbr, "u_csm_taps",
                               f->cfg.dir_pcf_taps == 16 ? 16 : 8);
    } else {
        /* The u_csm_* sampler2DArrays are declared in the program, so they must
         * be assigned distinct units (22/23) even when disabled -- two samplers
         * of different types sharing unit 0 makes every draw INVALID_OPERATION. */
        shader_uniform_set_int(&f->cache, &f->pbr, "u_csm_enabled", 0);
        shader_uniform_set_int(&f->cache, &f->pbr, "u_csm_static", 22);
        shader_uniform_set_int(&f->cache, &f->pbr, "u_dyn_map", 23);
    }
    /* Translucency mask samplers on 36-39 (GI probes hold 24-34). NULL-safe /
     * disabled-safe: always assigns the units so declarations never clash. */
    shadow_csm_mask_bind(&f->csm, &f->cache, &f->pbr, 36u, 37u, 38u, 39u);
    /* Caustic map on 40 (rpg-kbqd); assigns the unit even when off. */
    shadow_caustics_bind(&f->caustics, &f->cache, &f->pbr, 40u);

    /* Dynamic-GI (or other) extra binds: probe samplers on units 24+. When no
     * hook is set, force the probe path off but still assign the buffer samplers
     * distinct units so their declarations don't clash. */
    if (f->cfg.material_extra_bind != NULL) {
        f->cfg.material_extra_bind(f->cfg.material_extra_user, &f->cache, &f->pbr);
    } else {
        /* EVERY sampler the probe-GI path declares must sit on its own unit
         * even when GI is off: an unassigned sampler defaults to unit 0, and a
         * samplerBuffer/sampler3D sharing unit 0 with the sampler2D albedo is
         * a type conflict that makes every draw INVALID_OPERATION. Mirror the
         * gi_runtime_bind unit layout (base 24). */
        shader_uniform_set_int(&f->cache, &f->pbr, "u_gi_enabled", 0);
        shader_uniform_set_int(&f->cache, &f->pbr, "u_brick_on", 0);
        shader_uniform_set_int(&f->cache, &f->pbr, "u_probe_pos", 24);
        shader_uniform_set_int(&f->cache, &f->pbr, "u_probe_sh", 25);
        shader_uniform_set_int(&f->cache, &f->pbr, "u_probe_froxel_off", 26);
        shader_uniform_set_int(&f->cache, &f->pbr, "u_probe_froxel_cnt", 27);
        shader_uniform_set_int(&f->cache, &f->pbr, "u_probe_froxel_idx", 28);
        shader_uniform_set_int(&f->cache, &f->pbr, "u_probe_depth", 29);
        shader_uniform_set_int(&f->cache, &f->pbr, "u_probe_sg", 30);
        shader_uniform_set_int(&f->cache, &f->pbr, "u_brick_index", 31);
        shader_uniform_set_int(&f->cache, &f->pbr, "u_brick_meta", 32);
        shader_uniform_set_int(&f->cache, &f->pbr, "u_brick_pidx", 33);
        shader_uniform_set_int(&f->cache, &f->pbr, "u_probe_valid", 34);
        /* Reflection-probe samplers (rpg-akwc): unit 35 + 41, count 0. */
        shader_uniform_set_int(&f->cache, &f->pbr, "u_refl_atlas", 35);
        shader_uniform_set_int(&f->cache, &f->pbr, "u_refl_meta", 41);
        shader_uniform_set_int(&f->cache, &f->pbr, "u_refl_count", 0);
    }

    /* Frustum-cull the forward pass against the camera (rpg-0rs4). draw_distance
     * (0 = unlimited) adds a far cutoff. The depth pre-pass culls identically. */
    float planes[6][4];
    frustum_extract_planes_vp(s->camera.proj, s->camera.view, planes);

    for (uint32_t i = 0; i < s->count; ++i) {
        const render_renderable_t *r = &s->items[i];
        if (r->mesh == NULL)
            continue;
        if (frustum_cull_aabb_ex(planes, r->model, r->mesh->aabb_min,
                                 r->mesh->aabb_max, s->camera.eye,
                                 f->cfg.draw_distance))
            continue;
        /* Static renderables read the baked SH lightmap; dynamic ones (>=
         * dynamic_from) are not in the bake, so use flat ambient instead. */
        shader_uniform_set_float(&f->cache, &f->pbr, "u_sh_object",
                                 (i < s->dynamic_from) ? 1.0f : 0.0f);
        /* Per-chunk lightmap page for this mesh (rpg-yfa4). */
        shader_uniform_set_int(&f->cache, &f->pbr, "u_sh_layer", r->sh_layer);
        shader_uniform_set_mat4(&f->cache, &f->pbr, "u_model", r->model, 0);
        static_mesh_bind(r->mesh);
        /* Per-submesh material: a building's walls/glass/signs each shade with
         * their own material. Translucent submeshes (opacity < 1) draw in the
         * sorted blend pass after this one (rpg-rxf8), never here. */
        for (uint32_t sub = 0; sub < r->mesh->submesh_count; ++sub) {
            const render_material_t *m = render_submesh_material(s, r, sub);
            if (m == NULL || m->opacity < 0.999f)
                continue;
            material_bind(m, 0u, &f->cache, &f->pbr);
            static_mesh_draw_submesh(r->mesh, sub);
        }
    }
    if (f->overdraw && f->glDisable != NULL)      /* restore for the next pass. */
        f->glDisable(GL_BLEND);
}

/* Sorted translucent pass (rpg-rxf8): the 4th graph node, running right after
 * 'forward' on the same thread, so the PBR program + cluster/shadow bindings
 * it left in the context are still current. Collect every visible renderable
 * with opacity < 1, sort BACK-TO-FRONT by view-space AABB-centre depth, and
 * blend them over the opaque image: GL_BLEND (SRC_ALPHA, 1-SRC_ALPHA), depth
 * test LEQUAL against the opaque depth, depth WRITE off (a nearer glass pane
 * must not depth-kill a farther one). Full clustered lighting + u_opacity. */
static void fwd_translucent_submit(void *ud)
{
    render_forward_t *f = (render_forward_t *)ud;
    const render_scene_t *s = f->scene;
    if (s == NULL || f->trans_idx == NULL || f->glBlendFunc == NULL ||
        f->glDisable == NULL)
        return;

    float planes[6][4];
    frustum_extract_planes_vp(s->camera.proj, s->camera.view, planes);
    const float *v = s->camera.view;   /* column-major view matrix. */

    /* Collect TRANSLUCENT SUBMESHES (not whole items): a building's glass
     * windows are one submesh of a mostly-opaque mesh. Pack (item<<8 | sub);
     * sub < 256 by the loader's submesh cap. Sort by the item's AABB-centre
     * view depth (submeshes share the transform -- a fine approximation). */
    uint32_t n = 0, dropped = 0;
    for (uint32_t i = 0; i < s->count; ++i) {
        const render_renderable_t *r = &s->items[i];
        if (r->mesh == NULL)
            continue;
        if (frustum_cull_aabb_ex(planes, r->model, r->mesh->aabb_min,
                                 r->mesh->aabb_max, s->camera.eye,
                                 f->cfg.draw_distance))
            continue;
        float lc[3] = { (r->mesh->aabb_min[0] + r->mesh->aabb_max[0]) * 0.5f,
                        (r->mesh->aabb_min[1] + r->mesh->aabb_max[1]) * 0.5f,
                        (r->mesh->aabb_min[2] + r->mesh->aabb_max[2]) * 0.5f };
        const float *mm = r->model;
        float wc[3] = {
            mm[0] * lc[0] + mm[4] * lc[1] + mm[8] * lc[2] + mm[12],
            mm[1] * lc[0] + mm[5] * lc[1] + mm[9] * lc[2] + mm[13],
            mm[2] * lc[0] + mm[6] * lc[1] + mm[10] * lc[2] + mm[14],
        };
        float vz = v[2] * wc[0] + v[6] * wc[1] + v[10] * wc[2] + v[14];
        uint32_t nsub = r->mesh->submesh_count < 256u ? r->mesh->submesh_count : 255u;
        for (uint32_t sub = 0; sub < nsub; ++sub) {
            const render_material_t *m = render_submesh_material(s, r, sub);
            if (m == NULL || m->opacity >= 0.999f)
                continue;
            if (n >= f->trans_cap) { ++dropped; continue; }
            f->trans_idx[n] = (i << 8) | sub;
            f->trans_key[n] = -vz;      /* view looks down -z: -vz = distance. */
            ++n;
        }
    }
    if (dropped > 0u)
        fprintf(stderr, "render_forward: %u translucent submeshes over "
                "max_translucent=%u dropped this frame\n", dropped, f->trans_cap);
    if (n == 0u)
        return;

    /* Insertion sort, farthest first (translucent sets are small). */
    for (uint32_t i = 1; i < n; ++i) {
        uint32_t ix = f->trans_idx[i];
        float ky = f->trans_key[i];
        uint32_t j = i;
        for (; j > 0u && f->trans_key[j - 1] < ky; --j) {
            f->trans_idx[j] = f->trans_idx[j - 1];
            f->trans_key[j] = f->trans_key[j - 1];
        }
        f->trans_idx[j] = ix;
        f->trans_key[j] = ky;
    }

    f->depth.glEnable(GL_DEPTH_TEST);
    f->depth.glDepthFunc(GL_LEQUAL);
    f->depth.glDepthMask(0);           /* read the opaque depth, never write. */
    f->depth.glEnable(GL_BLEND);
    f->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    shader_program_bind(&f->pbr);

    uint32_t bound_item = 0xffffffffu;
    for (uint32_t k = 0; k < n; ++k) {
        uint32_t i = f->trans_idx[k] >> 8;
        uint32_t sub = f->trans_idx[k] & 0xffu;
        const render_renderable_t *r = &s->items[i];
        if (i != bound_item) {          /* per-item uniforms + VAO once. */
            shader_uniform_set_float(&f->cache, &f->pbr, "u_sh_object",
                                     (i < s->dynamic_from) ? 1.0f : 0.0f);
            shader_uniform_set_int(&f->cache, &f->pbr, "u_sh_layer", r->sh_layer);
            shader_uniform_set_mat4(&f->cache, &f->pbr, "u_model", r->model, 0);
            static_mesh_bind(r->mesh);
            bound_item = i;
        }
        const render_material_t *m = render_submesh_material(s, r, sub);
        if (m == NULL)
            continue;
        material_bind(m, 0u, &f->cache, &f->pbr);
        static_mesh_draw_submesh(r->mesh, sub);
    }

    f->depth.glDepthMask(1);
    f->glDisable(GL_BLEND);
}

/* ── Public API ── */

bool render_forward_init(render_forward_t *fwd, const render_forward_config_t *cfg)
{
    if (fwd == NULL || cfg == NULL || cfg->loader == NULL)
        return false;
    memset(fwd, 0, sizeof(*fwd));
    fwd->cfg = *cfg;
    fwd->prof = getenv("PROF") != NULL;
    if (fwd->prof && cfg->loader->get_proc_address) {
        void *p = cfg->loader->get_proc_address("glFinish", cfg->loader->user_data);
        memcpy(&fwd->glFinish, &p, sizeof p);
    }
    fwd->overdraw = getenv("PBR_OVERDRAW") != NULL;
    fwd->no_prepass = getenv("PBR_NOPREPASS") != NULL;
    if (cfg->loader->get_proc_address) {
        void *b = cfg->loader->get_proc_address("glBlendFunc", cfg->loader->user_data);
        void *d = cfg->loader->get_proc_address("glDisable", cfg->loader->user_data);
        void *g = cfg->loader->get_proc_address("glGetIntegerv", cfg->loader->user_data);
        void *f = cfg->loader->get_proc_address("glBindFramebuffer", cfg->loader->user_data);
        memcpy(&fwd->glBlendFunc, &b, sizeof b);
        memcpy(&fwd->glDisable, &d, sizeof d);
        memcpy(&fwd->glGetIntegerv, &g, sizeof g);
        memcpy(&fwd->glBindFramebuffer, &f, sizeof f);
    }

    char log[1024] = { 0 };
    if (pbr_shader_create(&fwd->pbr, cfg->loader, log, sizeof log) != SHADER_PROGRAM_OK)
        return false;
    shader_uniform_cache_init(&fwd->cache, &fwd->pbr);
    if (depth_prepass_init(&fwd->depth, cfg->loader) != SHADER_PROGRAM_OK ||
        !forward_plus_init(&fwd->fp, cfg->loader)) {
        render_forward_destroy(fwd);
        return false;
    }
    if (cfg->shadow_res > 0u &&
        !shadow_cube_init(&fwd->shadow, cfg->loader, cfg->shadow_res,
                          cfg->shadow_near, cfg->shadow_far,
                          cfg->shadow_max ? cfg->shadow_max : 16u)) {
        render_forward_destroy(fwd);
        return false;
    }
    fwd->shadow_slot = malloc((size_t)cfg->max_lights * sizeof(int));
    if (fwd->shadow_slot == NULL) { render_forward_destroy(fwd); return false; }
    for (uint32_t i = 0; i < cfg->max_lights; ++i) fwd->shadow_slot[i] = -1;
    if (cfg->spot_res > 0u &&
        !shadow_spot_init(&fwd->spot, cfg->loader, cfg->spot_res,
                          cfg->spot_near, cfg->spot_far)) {
        render_forward_destroy(fwd);
        return false;
    }
    if (cfg->dir_cascades > 0u) {
        shadow_csm_config_t sc = {
            .loader = cfg->loader,
            .cascades = cfg->dir_cascades,
            .static_res = cfg->dir_static_res ? cfg->dir_static_res : 2048u,
            .dynamic_res = cfg->dir_dynamic_res ? cfg->dir_dynamic_res : 1024u,
            .lambda = cfg->dir_lambda,
            .max_distance = cfg->dir_max_distance,
            .softness = cfg->dir_softness,
            .pcss = cfg->dir_pcss,
            .msaa = cfg->dir_msaa,
            .mask_res = cfg->dir_mask_res,
        };
        if (!shadow_csm_init(&fwd->csm, &sc)) {
            render_forward_destroy(fwd);
            return false;
        }
        /* Translucency mask (rpg-29zj): tint/depth targets for translucent sun
         * casters. Init failure is non-fatal -- the mask stays disabled and
         * translucent casters fall back to hard shadows in the main maps. */
        if (cfg->dir_translucency)
            (void)shadow_csm_mask_init(&fwd->csm, cfg->loader);
        /* Caustics (rpg-kbqd) refine the mask's flat tint via SDF-traced
         * light-space splats. GL 4.3 compute only; failure is non-fatal. */
        if (cfg->dir_caustics && fwd->csm.mask_enabled) {
            /* Quarter-res caustic map: the trace reads the mask through
             * normalised uv (resolutions are decoupled) and glass shadows are
             * soft -- 16x cheaper bakes for no visible loss. */
            shadow_caustics_config_t cc = {
                .loader = cfg->loader,
                .resolution = sc.static_res / 4u ? sc.static_res / 4u : 256u,
                .cascades = sc.cascades,
                .samples = 8u,
                .scatter = 0.0f,     /* specular glass by default. */
                .scatter_dist = 1.0f,
                .max_dist = 64.0f,
            };
            (void)shadow_caustics_init(&fwd->caustics, &cc);
        }
    }

    uint32_t ctot = cfg->cluster.tiles_x * cfg->cluster.tiles_y * cfg->cluster.slices;
    fwd->offsets = malloc((size_t)ctot * sizeof(uint32_t));
    fwd->counts = malloc((size_t)ctot * sizeof(uint32_t));
    fwd->indices = malloc((size_t)cfg->index_capacity * sizeof(uint32_t));
    fwd->light_data = malloc((size_t)cfg->max_lights * 16u * sizeof(float));
    /* Per-SUBMESH now (glass is a submesh of each building), so size generously. */
    fwd->trans_cap = cfg->max_translucent ? cfg->max_translucent : 8192u;
    fwd->trans_idx = malloc((size_t)fwd->trans_cap * sizeof(uint32_t));
    fwd->trans_key = malloc((size_t)fwd->trans_cap * sizeof(float));
    if (fwd->offsets == NULL || fwd->counts == NULL || fwd->indices == NULL ||
        fwd->light_data == NULL || fwd->trans_idx == NULL ||
        fwd->trans_key == NULL) {
        render_forward_destroy(fwd);
        return false;
    }
    cluster_grid_init(&fwd->clusters, cfg->cluster, fwd->offsets, fwd->counts,
                      fwd->indices, cfg->index_capacity);

    /* Wire the four graph nodes: depth_pre (optional) and light_cull have no
     * deps; forward waits for both (a disabled depth_pre dep is auto-satisfied);
     * the sorted translucent pass (rpg-rxf8) waits for forward. */
    fwd->passes[0] = (render_pass_t){ "depth_pre", NULL, fwd_depth_submit, NULL,
                                      fwd, RENDER_PASS_DEPTH_PRE, NULL, 0u };
    fwd->passes[1] = (render_pass_t){ "light_cull", NULL, fwd_cull_submit, NULL,
                                      fwd, RENDER_PASS_LIGHT_CULL, NULL, 0u };
    fwd->passes[2] = (render_pass_t){ "forward", NULL, fwd_forward_submit, NULL,
                                      fwd, RENDER_PASS_FORWARD, NULL, 0u };
    fwd->passes[3] = (render_pass_t){ "translucent", NULL, fwd_translucent_submit,
                                      NULL, fwd, RENDER_PASS_FORWARD, NULL, 0u };
    fwd->dep_fwd[0] = "depth_pre";
    fwd->dep_fwd[1] = "light_cull";
    fwd->dep_trans[0] = "forward";
    fwd->nodes[0] = (render_pipeline_graph_node_t){
        &fwd->passes[0], NULL, 0u, RENDER_PIPELINE_NODE_FLAG_DEPTH_PREPASS };
    fwd->nodes[1] = (render_pipeline_graph_node_t){ &fwd->passes[1], NULL, 0u, 0u };
    fwd->nodes[2] = (render_pipeline_graph_node_t){ &fwd->passes[2], fwd->dep_fwd,
                                                    2u, 0u };
    fwd->nodes[3] = (render_pipeline_graph_node_t){ &fwd->passes[3], fwd->dep_trans,
                                                    1u, 0u };
    fwd->graph.nodes = fwd->nodes;
    fwd->graph.node_count = 4u;
    return true;
}

/* PROF: glFinish + stamp; returns ms since *prev and advances it. */
static double fr_prof_tick(render_forward_t *fwd, struct timespec *prev)
{
    if (!fwd->prof || fwd->glFinish == NULL) return 0.0;
    fwd->glFinish();
    struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
    double ms = (now.tv_sec - prev->tv_sec) * 1e3 + (now.tv_nsec - prev->tv_nsec) * 1e-6;
    *prev = now;
    return ms;
}

void render_forward_render(render_forward_t *fwd, const render_scene_t *scene)
{
    if (fwd == NULL || scene == NULL)
        return;
    fwd->scene = scene;
    static double a_cube = 0, a_csm = 0, a_fwd = 0; static int a_n = 0;
    struct timespec pt;
    if (fwd->prof && fwd->glFinish) { fwd->glFinish(); clock_gettime(CLOCK_MONOTONIC, &pt); }
    /* Render an omnidirectional cube shadow for EVERY point light tagged
     * RENDER_LIGHT_FLAG_SHADOW, each into its own cube-array slot (assigned here,
     * consumed by fwd_cull_submit -> light_data.t3.y). Then restore the viewport. */
    if (fwd->cfg.shadow_res > 0u && scene->lights != NULL && fwd->shadow_slot != NULL) {
        FR_ZONE(z_cube, "Render.Shadow.Cube");
        uint32_t n = scene->lights->count;
        if (n > fwd->cfg.max_lights) n = fwd->cfg.max_lights;
        /* shadow_distance (0 = unlimited): a light whose position is beyond this
         * range from the camera gets no cube shadow this frame (rpg-9u96). */
        const float sdist = fwd->cfg.shadow_distance;
        const float sd2 = sdist * sdist;
        const float *eye = scene->camera.eye;
        /* Which lights want a shadow slot this frame? Decide first so we can skip
         * the whole pass (and its full-array clear) when none do. */
        uint32_t want = 0;
        for (uint32_t i = 0; i < n; ++i) {
            const render_light_t *L = &scene->lights->lights[i];
            int shadowed = (L->kind == RENDER_LIGHT_POINT || L->kind == RENDER_LIGHT_SPOT) &&
                           (L->flags & RENDER_LIGHT_FLAG_SHADOW);
            if (shadowed && sdist > 0.0f) {
                float dx = L->position[0]-eye[0], dy = L->position[1]-eye[1],
                      dz = L->position[2]-eye[2];
                if (dx*dx + dy*dy + dz*dz > sd2) shadowed = 0;  /* too far to matter. */
            }
            fwd->shadow_slot[i] = shadowed ? 0 : -1;  /* mark; real slot assigned below. */
            want += (uint32_t)(shadowed != 0);
        }
        if (want > 0u) {
            uint32_t slot = 0;
            shadow_cube_clear(&fwd->shadow);  /* clear all layers once (layered FBO). */
            for (uint32_t i = 0; i < n; ++i) {
                const render_light_t *L = &scene->lights->lights[i];
                if (fwd->shadow_slot[i] >= 0 && slot < fwd->shadow.max_lights) {
                    shadow_cube_render_light(&fwd->shadow, scene, L->position, slot,
                                             L->range);
                    fwd->shadow_slot[i] = (int)slot;
                    ++slot;
                } else {
                    fwd->shadow_slot[i] = -1;
                }
            }
            fwd->shadow.glViewport(0, 0, (int32_t)fwd->cfg.screen_w,
                                   (int32_t)fwd->cfg.screen_h);
        }
        FR_ZONE_END(z_cube);
    }
    a_cube += fr_prof_tick(fwd, &pt);
    if (fwd->cfg.spot_res > 0u && fwd->cfg.spot_light >= 0 &&
        scene->lights != NULL &&
        (uint32_t)fwd->cfg.spot_light < scene->lights->count) {
        const render_light_t *sl = &scene->lights->lights[fwd->cfg.spot_light];
        /* Full cone angle from the outer cutoff cosine (fall back to 60 deg). */
        float fov = (sl->cos_outer > -1.0f && sl->cos_outer < 1.0f)
                        ? 2.0f * acosf(sl->cos_outer) : 1.0472f;
        shadow_spot_render(&fwd->spot, scene, sl->position, sl->direction, fov);
        fwd->spot.glViewport(0, 0, (int32_t)fwd->cfg.screen_w,
                             (int32_t)fwd->cfg.screen_h);
    }
    if (fwd->cfg.dir_cascades > 0u) {
        FR_ZONE(z_csm, "Render.Shadow.CSM");
        /* Sun travels along -u_sun_dir (u_sun_dir points toward the sun). Fit the
         * cascades to the camera frustum, bake the static casters once (cached),
         * and re-render only the dynamic casters this frame. */
        float td[3] = { -fwd->cfg.sun_dir[0], -fwd->cfg.sun_dir[1],
                        -fwd->cfg.sun_dir[2] };
        /* View-independent cascade fit: classify casters by size and fit each to
         * its own bounds (coarse cascade = whole scene AABB when set), so tall /
         * off-view / behind-camera casters are never clipped. Cached forever. */
        int has_b = (fwd->cfg.shadow_scene_max[0] > fwd->cfg.shadow_scene_min[0]);
        shadow_csm_update(&fwd->csm, scene, td,
                          has_b ? fwd->cfg.shadow_scene_min : NULL,
                          has_b ? fwd->cfg.shadow_scene_max : NULL);
        shadow_csm_bake_static(&fwd->csm, scene);
        shadow_csm_render_dynamic(&fwd->csm, scene);
        /* Translucency mask piggybacks on the cascade matrices just computed. */
        shadow_csm_mask_bake_static(&fwd->csm, scene);
        shadow_csm_mask_render_dynamic(&fwd->csm, scene);
        /* Caustics: SDF-trace the freshly baked static mask, ONE cascade per
         * frame (a full-burst bake is a visible hitch). The bake shares one
         * layer index across the color/depth atlases -- mask_init creates
         * them as twins, so their bases always match. */
        if (!fwd->caustics_baked && fwd->csm.mask_static_valid &&
            fwd->caustics.map_tex != 0u &&
            fwd->csm.mask_color_base == fwd->csm.mask_depth_base) {
            uint32_t cci = fwd->caustic_next;
            shadow_caustics_bake(&fwd->caustics,
                                 fwd->csm.mask_color_atlas.texture,
                                 fwd->csm.mask_depth_atlas.texture,
                                 (uint32_t)fwd->csm.mask_color_base + cci,
                                 cci, fwd->csm.view_proj[cci].m,
                                 fwd->csm.eye[cci],
                                 fwd->csm.far_plane[cci]);
            if (++fwd->caustic_next >= fwd->csm.cascades) {
                fwd->caustic_next = 0;
                fwd->caustics_baked = true;
            }
        }
        fwd->csm.glViewport(0, 0, (int32_t)fwd->cfg.screen_w,
                            (int32_t)fwd->cfg.screen_h);
        FR_ZONE_END(z_csm);
    }
    a_csm += fr_prof_tick(fwd, &pt);
    /* The shadow pre-passes (and GI compute, streaming uploads, ...) leave
     * arbitrary FBOs bound, so bind the EXPLICIT render target before the
     * main graph: fwd->target_fbo, default 0 = the window. Never inherit
     * whatever framebuffer happened to be bound -- a stale binding from an
     * earlier pass would silently swallow the frame. */
    if (fwd->glBindFramebuffer)
        fwd->glBindFramebuffer(GL_FRAMEBUFFER, fwd->target_fbo);
    /* Early-Z only pays off with overlapping geometry; skip it for a single
     * renderable so a trivial scene still executes (depth_pre is skippable). */
    int depth_enabled = (scene->count > 1u) ? 1 : 0;
    FR_ZONE(z_fwd, "Render.Forward.Graph");
    render_pipeline_graph_execute(&fwd->graph, depth_enabled);
    FR_ZONE_END(z_fwd);
    a_fwd += fr_prof_tick(fwd, &pt);
    if (fwd->prof && fwd->glFinish && ++a_n >= 60) {
        fprintf(stderr, "[prof] cube=%.2f  csm=%.2f  forward=%.2f  (ms/frame, GPU-incl)\n",
                a_cube / a_n, a_csm / a_n, a_fwd / a_n);
        a_cube = a_csm = a_fwd = 0; a_n = 0;
    }
    fwd->scene = NULL;
}

void render_forward_destroy(render_forward_t *fwd)
{
    if (fwd == NULL)
        return;
    shadow_cube_destroy(&fwd->shadow);
    shadow_spot_destroy(&fwd->spot);
    shadow_caustics_destroy(&fwd->caustics);
    shadow_csm_destroy(&fwd->csm);
    forward_plus_destroy(&fwd->fp);
    depth_prepass_destroy(&fwd->depth);
    shader_program_destroy(&fwd->pbr);
    free(fwd->trans_idx);
    free(fwd->trans_key);
    free(fwd->shadow_slot);
    free(fwd->offsets);
    free(fwd->counts);
    free(fwd->indices);
    free(fwd->light_data);
    fwd->offsets = NULL;
    fwd->counts = NULL;
    fwd->indices = NULL;
    fwd->light_data = NULL;
}
