/**
 * @file render_world_init.c
 * @brief render_world assembly + teardown (rpg-i3wx).
 */
#include <string.h>

#include "ferrum/renderer/render_world.h"

/* GI bind hook: the sole forward+ <-> GI coupling, at texture unit 24. */
static void rw_gi_bind(void *u, shader_uniform_cache_t *c, const shader_program_t *p)
{
    gi_runtime_bind((gi_runtime_t *)u, c, p, 24u);
}

bool render_world_init(render_world_t *rw, const render_world_config_t *cfg)
{
    if (rw == NULL || cfg == NULL || cfg->scene == NULL) return false;
    memset(rw, 0, sizeof *rw);
    rw->scene = cfg->scene;

    /* Init GI first: it only needs the cluster CONFIG (not the inited forward),
     * and forward wants a valid GI to bind. On GI failure, degrade gracefully to
     * no-GI (matching the demo) by not installing the bind hook. */
    rw->gi_enabled = 0;
    if (cfg->gi_enabled) {
        gi_runtime_config_t gc; memset(&gc, 0, sizeof gc);
        gc.loader = cfg->forward.loader;
        gc.sdf_prefix = cfg->gi_sdf_prefix;
        for (int i = 0; i < 3; ++i) {
            gc.aabb_min[i] = cfg->gi_aabb_min[i];
            gc.aabb_max[i] = cfg->gi_aabb_max[i];
        }
        gc.probe_pos_in = cfg->gi_probe_pos;
        gc.n_probe_in = cfg->gi_probe_count;
        gc.grid_cell = cfg->gi_grid_cell;
        gc.prepass_w = cfg->gi_prepass_w;
        gc.prepass_h = cfg->gi_prepass_h;
        gc.max_lights = cfg->gi_max_lights;
        gc.max_boxes = cfg->gi_max_boxes;
        gc.soft_k = cfg->gi_soft_k;
        gc.froxel = cfg->forward.cluster;   /* CONTRACT: identical froxels. */
        gc.probe_min = cfg->gi_probe_min;
        gc.probe_sphere_margin = cfg->gi_probe_sphere_margin;
        gc.bin_interval = cfg->gi_bin_interval;
        gc.update_interval = cfg->gi_update_interval;
        gc.n_probe_groups = cfg->gi_n_probe_groups;
        if (gi_runtime_init(&rw->gi, &gc)) {
            rw->gi_enabled = 1;
            if (cfg->gi_grid_dim[0] > 0)
                gi_runtime_set_probe_grid(&rw->gi, cfg->gi_grid_origin,
                                          cfg->gi_grid_cell3, cfg->gi_grid_dim);
            if (cfg->has_static_volume)
                gi_runtime_set_static_volume(&rw->gi, cfg->static_vol_tex,
                                             cfg->static_vol_origin, cfg->static_vol_dim,
                                             cfg->static_vol_voxel, cfg->static_k);
            if (cfg->has_static_weights)
                gi_runtime_set_static_weights(&rw->gi, cfg->static_baked_w,
                                              cfg->static_dyn_w);
            if (cfg->has_sky_ao)
                gi_runtime_set_sky_ao(&rw->gi, cfg->sky_ao_color, cfg->sky_ao_ref,
                                      cfg->sky_ao_mult);
            if (cfg->has_spec_gain)
                gi_runtime_set_spec_gain(&rw->gi, cfg->spec_gain);
        }
    }

    render_forward_config_t fc = cfg->forward;
    if (rw->gi_enabled) {
        fc.material_extra_bind = rw_gi_bind;
        fc.material_extra_user = &rw->gi;
    }
    if (!render_forward_init(&rw->forward, &fc)) {
        if (rw->gi_enabled) gi_runtime_destroy(&rw->gi);
        return false;
    }
    return true;
}

void render_world_destroy(render_world_t *rw)
{
    if (rw == NULL) return;
    if (rw->gi_enabled) gi_runtime_destroy(&rw->gi);
    render_forward_destroy(&rw->forward);
    rw->scene = NULL;
}
