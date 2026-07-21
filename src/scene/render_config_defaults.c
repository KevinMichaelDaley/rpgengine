/**
 * @file render_config_defaults.c
 * @brief Engine default render-world tuning (rpg-da8c). These values mirror the
 *        working hall_lit_dynamic setup + client_scene_load so a level with NO
 *        render config renders identically to before the config was extracted.
 */
#include <string.h>

#include "ferrum/scene/render_config.h"

void render_config_defaults(render_config_t *rc)
{
    if (rc == NULL) return;
    memset(rc, 0, sizeof *rc);

    /* Forward+ froxel cluster (hall_lit_dynamic fcfg.cluster). */
    rc->cluster_tiles_x = 16; rc->cluster_tiles_y = 16; rc->cluster_slices = 24;
    rc->cluster_near = 0.2f;  rc->cluster_far = 60.0f;
    rc->max_lights = 512;

    /* Baked lightmap. sh_scale < 1: the lightmap is INDIRECT (direct is the
     * realtime sun + CSM + clustered punctual lights). sh_enabled=-1 => auto (on
     * iff a lightmap texture is present); set 0 to force the lightmap OFF. */
    rc->sh_enabled = -1;
    rc->sh_scale = 0.7f;
    /* GI diffuse normal = mix(geometric, normal-mapped) by this. It drives BOTH the
     * baked-lightmap diffuse and the probe diffuse, so a low value averages the
     * normal map away and GI-lit surfaces read flat/detail-less. Keep it close to
     * the mapped normal (the renderer's own default is 1). */
    rc->sh_normal_bias = 0.9f;
    rc->ambient[0] = rc->ambient[1] = rc->ambient[2] = 0.0f;

    /* Sun + cascaded shadow maps. */
    rc->sun_energy_scale = 0.45f;
    rc->dir_cascades = 2;
    rc->dir_static_res = 1024; rc->dir_dynamic_res = 1024;
    rc->dir_lambda = 0.6f; rc->dir_bias = 0.05f; rc->dir_softness = 0.7f;
    rc->dir_max_distance = 0.0f; rc->dir_pcss = 0;

    /* Point (cube) shadows. shadow_far = scene diag * far_scale. */
    rc->shadow_max = 8; rc->shadow_res = 256;
    rc->shadow_near = 0.1f; rc->shadow_far_scale = 1.2f; rc->shadow_bias = 0.08f;

    /* GI runtime. */
    rc->gi_enabled = 1;
    rc->gi_grid_cell = 4.0f; rc->gi_soft_k = 8.0f;
    rc->gi_max_lights = 512; rc->gi_max_boxes = 64; rc->gi_probe_min = 4;
    rc->gi_probe_sphere_margin = 1.2f; rc->gi_bin_interval = 1;
    rc->gi_update_interval = 8; rc->gi_n_probe_groups = 2;
    rc->gi_smooth = 0.15f;            /* steady probe temporal-EMA blend. */
    /* Probe-GI tuning (formerly GI_* env only). */
    rc->gi_field = 1; rc->gi_mis = 0; rc->gi_hybrid = 0; rc->gi_hero = 2;
    rc->gi_samples = 24; rc->gi_spec_lobes = 2;
    rc->gi_bounce = 0.9f;   /* NOTE steady state = 1/(1-gain): 0.9 settles at 10x. */
    /* Offline brick placement (probe_bake tool). Coarse 9 m / 3 levels => 1 m
     * finest bricks with 0.33 m probe spacing near geometry. */
    rc->gi_brick_coarse = 9.0f;
    rc->gi_brick_levels = 3;
    rc->gi_brick_fill = 1;
    rc->gi_fixup_clearance = 0.10f;
    rc->gi_fixup_max_push = 0.60f;
    rc->gi_near = 2.2f; rc->gi_dmax = 2.5f; rc->gi_emin = 0.02f;
    rc->gi_norm_gate = 0.75f; rc->gi_stat_scale = 1.0f;
    rc->gi_vis_bias = 0.30f; rc->gi_vis_varmin = 0.02f; rc->gi_vis_sharp = 1.0f;
    rc->aniso = 16.0f; rc->msaa = 4;
    rc->probe_spacing_scale = 1.0f;   /* 1 = descriptor spacing as authored. */
    /* Probe AABB inset (client nudges min.y up, max.y down to avoid floor/ceiling). */
    rc->gi_aabb_pad_lo[0] = 0.0f; rc->gi_aabb_pad_lo[1] = 0.3f; rc->gi_aabb_pad_lo[2] = 0.0f;
    rc->gi_aabb_pad_hi[0] = 0.0f; rc->gi_aabb_pad_hi[1] = 0.2f; rc->gi_aabb_pad_hi[2] = 0.0f;

    /* Static-irradiance / spec / sky-AO weights. */
    rc->static_baked_w = 0.35f; rc->static_dyn_w = 3.0f;
    rc->static_k = 1.0f; rc->spec_gain = 1.0f;
    rc->sky_ao_color[0] = 0.15390f * 0.25f;
    rc->sky_ao_color[1] = 0.18851f * 0.25f;
    rc->sky_ao_color[2] = 0.25879f * 0.25f;
    rc->sky_ao_ref = 5.0f; rc->sky_ao_mult = 0.6f;
}
