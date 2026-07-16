/**
 * @file render_forward.c
 * @brief Clustered forward+ pipeline driver (see render_forward.h).
 */
#include "ferrum/renderer/render_forward.h"

#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/renderer/gl_constants.h"
#include "ferrum/renderer/material.h"
#include "ferrum/renderer/mesh/static_mesh.h"
#include "ferrum/renderer/pbr_shader.h"

/* ── Pass callbacks (driven from the graph nodes; user_data == driver) ── */

/* Depth pre-pass: colour-masked depth-only draw of every renderable. */
static void fwd_depth_submit(void *ud)
{
    render_forward_t *f = (render_forward_t *)ud;
    if (f->scene != NULL)
        depth_prepass_execute(&f->depth, f->scene);
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
    for (uint32_t i = 0; i < n; ++i)
        forward_plus_pack_light(&lights[i], &f->light_data[i * 16]);
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

    shader_program_bind(&f->pbr);
    shader_uniform_set_mat4(&f->cache, &f->pbr, "u_view", s->camera.view, 0);
    shader_uniform_set_mat4(&f->cache, &f->pbr, "u_projection", s->camera.proj, 0);
    shader_uniform_set_vec3(&f->cache, &f->pbr, "u_eye_pos", s->camera.eye);
    shader_uniform_set_vec3(&f->cache, &f->pbr, "u_sun_dir", f->cfg.sun_dir);
    shader_uniform_set_vec3(&f->cache, &f->pbr, "u_sun_color", f->cfg.sun_color);
    shader_uniform_set_vec3(&f->cache, &f->pbr, "u_ambient", f->cfg.ambient);
    shader_uniform_set_int(&f->cache, &f->pbr, "u_light_count", 0);
    { const char *dbg = getenv("PBR_DEBUG");
      shader_uniform_set_int(&f->cache, &f->pbr, "u_debug_mode", dbg ? atoi(dbg) : 0); }
    forward_plus_bind(&f->fp, &f->cache, &f->pbr, &f->cfg.cluster,
                      f->cfg.screen_w, f->cfg.screen_h);

    /* Optional baked SH lightmap on units 7..15 (material uses 0..6, forward+
     * 16..19). Combines static GI (SH) with the clustered dynamic lights. */
    if (f->cfg.sh_enabled) {
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
        shader_uniform_set_int(&f->cache, &f->pbr, "u_sh_enabled", 1);
        shader_uniform_set_float(&f->cache, &f->pbr, "u_sh_scale",
                                 f->cfg.sh_scale > 0.0f ? f->cfg.sh_scale : 1.0f);
    } else {
        shader_uniform_set_int(&f->cache, &f->pbr, "u_sh_enabled", 0);
    }

    /* Optional point-light cube shadow on unit 20 (the movable light whose flat
     * index is shadow_light). Rendered before this pass in render_forward_render. */
    if (f->cfg.shadow_light >= 0 && f->cfg.shadow_res > 0u) {
        shadow_cube_bind(&f->shadow, &f->cache, &f->pbr, 20u);
        shader_uniform_set_int(&f->cache, &f->pbr, "u_shadow_light", f->cfg.shadow_light);
        shader_uniform_set_float(&f->cache, &f->pbr, "u_shadow_bias", f->cfg.shadow_bias);
    } else {
        shader_uniform_set_int(&f->cache, &f->pbr, "u_shadow_light", -1);
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
    } else {
        /* The u_csm_* sampler2DArrays are declared in the program, so they must
         * be assigned distinct units (22/23) even when disabled -- two samplers
         * of different types sharing unit 0 makes every draw INVALID_OPERATION. */
        shader_uniform_set_int(&f->cache, &f->pbr, "u_csm_enabled", 0);
        shader_uniform_set_int(&f->cache, &f->pbr, "u_csm_static", 22);
        shader_uniform_set_int(&f->cache, &f->pbr, "u_dyn_map", 23);
    }

    for (uint32_t i = 0; i < s->count; ++i) {
        const render_renderable_t *r = &s->items[i];
        if (r->mesh == NULL)
            continue;
        if (r->material != NULL)
            material_bind(r->material, 0u, &f->cache, &f->pbr);
        /* Static renderables read the baked SH lightmap; dynamic ones (>=
         * dynamic_from) are not in the bake, so use flat ambient instead. */
        shader_uniform_set_float(&f->cache, &f->pbr, "u_sh_object",
                                 (i < s->dynamic_from) ? 1.0f : 0.0f);
        /* Per-chunk lightmap page for this mesh (rpg-yfa4). */
        shader_uniform_set_int(&f->cache, &f->pbr, "u_sh_layer", r->sh_layer);
        shader_uniform_set_mat4(&f->cache, &f->pbr, "u_model", r->model, 0);
        static_mesh_bind(r->mesh);
        for (uint32_t sub = 0; sub < r->mesh->submesh_count; ++sub)
            static_mesh_draw_submesh(r->mesh, sub);
    }
}

/* ── Public API ── */

bool render_forward_init(render_forward_t *fwd, const render_forward_config_t *cfg)
{
    if (fwd == NULL || cfg == NULL || cfg->loader == NULL)
        return false;
    memset(fwd, 0, sizeof(*fwd));
    fwd->cfg = *cfg;

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
                          cfg->shadow_near, cfg->shadow_far)) {
        render_forward_destroy(fwd);
        return false;
    }
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
        };
        if (!shadow_csm_init(&fwd->csm, &sc)) {
            render_forward_destroy(fwd);
            return false;
        }
    }

    uint32_t ctot = cfg->cluster.tiles_x * cfg->cluster.tiles_y * cfg->cluster.slices;
    fwd->offsets = malloc((size_t)ctot * sizeof(uint32_t));
    fwd->counts = malloc((size_t)ctot * sizeof(uint32_t));
    fwd->indices = malloc((size_t)cfg->index_capacity * sizeof(uint32_t));
    fwd->light_data = malloc((size_t)cfg->max_lights * 16u * sizeof(float));
    if (fwd->offsets == NULL || fwd->counts == NULL || fwd->indices == NULL ||
        fwd->light_data == NULL) {
        render_forward_destroy(fwd);
        return false;
    }
    cluster_grid_init(&fwd->clusters, cfg->cluster, fwd->offsets, fwd->counts,
                      fwd->indices, cfg->index_capacity);

    /* Wire the three graph nodes: depth_pre (optional) and light_cull have no
     * deps; forward waits for both (a disabled depth_pre dep is auto-satisfied). */
    fwd->passes[0] = (render_pass_t){ "depth_pre", NULL, fwd_depth_submit, NULL,
                                      fwd, RENDER_PASS_DEPTH_PRE, NULL, 0u };
    fwd->passes[1] = (render_pass_t){ "light_cull", NULL, fwd_cull_submit, NULL,
                                      fwd, RENDER_PASS_LIGHT_CULL, NULL, 0u };
    fwd->passes[2] = (render_pass_t){ "forward", NULL, fwd_forward_submit, NULL,
                                      fwd, RENDER_PASS_FORWARD, NULL, 0u };
    fwd->dep_fwd[0] = "depth_pre";
    fwd->dep_fwd[1] = "light_cull";
    fwd->nodes[0] = (render_pipeline_graph_node_t){
        &fwd->passes[0], NULL, 0u, RENDER_PIPELINE_NODE_FLAG_DEPTH_PREPASS };
    fwd->nodes[1] = (render_pipeline_graph_node_t){ &fwd->passes[1], NULL, 0u, 0u };
    fwd->nodes[2] = (render_pipeline_graph_node_t){ &fwd->passes[2], fwd->dep_fwd,
                                                    2u, 0u };
    fwd->graph.nodes = fwd->nodes;
    fwd->graph.node_count = 3u;
    return true;
}

void render_forward_render(render_forward_t *fwd, const render_scene_t *scene)
{
    if (fwd == NULL || scene == NULL)
        return;
    fwd->scene = scene;
    /* Render the point-light cube shadow first (own FBO), then restore the main
     * viewport so the graph's passes draw at screen resolution. */
    if (fwd->cfg.shadow_res > 0u && fwd->cfg.shadow_light >= 0 &&
        scene->lights != NULL &&
        (uint32_t)fwd->cfg.shadow_light < scene->lights->count) {
        shadow_cube_render(&fwd->shadow, scene,
                           scene->lights->lights[fwd->cfg.shadow_light].position);
        fwd->shadow.glViewport(0, 0, (int32_t)fwd->cfg.screen_w,
                               (int32_t)fwd->cfg.screen_h);
    }
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
        /* Sun travels along -u_sun_dir (u_sun_dir points toward the sun). Fit the
         * cascades to the camera frustum, bake the static casters once (cached),
         * and re-render only the dynamic casters this frame. */
        float td[3] = { -fwd->cfg.sun_dir[0], -fwd->cfg.sun_dir[1],
                        -fwd->cfg.sun_dir[2] };
        /* Fit the cascades to the whole scene AABB (if set) so tall/off-view
         * casters are never clipped from the shadow map. */
        int has_b = (fwd->cfg.shadow_scene_max[0] > fwd->cfg.shadow_scene_min[0]);
        shadow_csm_update(&fwd->csm, &scene->camera, td,
                          has_b ? fwd->cfg.shadow_scene_min : NULL,
                          has_b ? fwd->cfg.shadow_scene_max : NULL);
        shadow_csm_bake_static(&fwd->csm, scene);
        shadow_csm_render_dynamic(&fwd->csm, scene);
        fwd->csm.glViewport(0, 0, (int32_t)fwd->cfg.screen_w,
                            (int32_t)fwd->cfg.screen_h);
    }
    /* Early-Z only pays off with overlapping geometry; skip it for a single
     * renderable so a trivial scene still executes (depth_pre is skippable). */
    int depth_enabled = (scene->count > 1u) ? 1 : 0;
    render_pipeline_graph_execute(&fwd->graph, depth_enabled);
    fwd->scene = NULL;
}

void render_forward_destroy(render_forward_t *fwd)
{
    if (fwd == NULL)
        return;
    shadow_cube_destroy(&fwd->shadow);
    shadow_spot_destroy(&fwd->spot);
    shadow_csm_destroy(&fwd->csm);
    forward_plus_destroy(&fwd->fp);
    depth_prepass_destroy(&fwd->depth);
    shader_program_destroy(&fwd->pbr);
    free(fwd->offsets);
    free(fwd->counts);
    free(fwd->indices);
    free(fwd->light_data);
    fwd->offsets = NULL;
    fwd->counts = NULL;
    fwd->indices = NULL;
    fwd->light_data = NULL;
}
