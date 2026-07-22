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
    rc->dir_translucency = 1; rc->dir_caustics = 1;

    /* Point (cube) shadows. shadow_far = scene diag * far_scale. */
    rc->shadow_max = 8; rc->shadow_res = 256;
    rc->shadow_near = 0.1f; rc->shadow_far_scale = 1.2f; rc->shadow_bias = 0.08f;

    /* GI runtime. */
    rc->gi_enabled = 1;
    rc->gi_grid_cell = 4.0f; rc->gi_soft_k = 8.0f;
    rc->gi_max_lights = 512; rc->gi_max_boxes = 64; rc->gi_probe_min = 4;
    rc->gi_probe_sphere_margin = 1.2f; rc->gi_bin_interval = 1;
    rc->gi_update_interval = 8; rc->gi_n_probe_groups = 2;
    rc->gi_freeze_ticks = 0;
    rc->gi_smooth = 0.15f;            /* steady probe temporal-EMA blend. */
    /* Probe-GI tuning (formerly GI_* env only). */
    rc->gi_field = 1; rc->gi_mis = 0; rc->gi_hybrid = 0; rc->gi_hero = 2;
    rc->gi_samples = 24; rc->gi_spec_lobes = 2;
    /* rpg-96ia: steady state = 1/(1-gain). 0.9 settled at ~10x (over-bright/
     * cadence-dependent); every shipped config already used 0.45. */
    rc->gi_bounce = 0.45f;
    rc->gi_near = 2.2f; rc->gi_dmax = 2.5f; rc->gi_emin = 0.02f;
    rc->gi_norm_gate = 0.75f; rc->gi_stat_scale = 1.0f; rc->gi_dyn_gain = 1.0f;
    rc->gi_vis_bias = 0.30f; rc->gi_vis_varmin = 0.02f; rc->gi_vis_sharp = 1.0f;
    /* Offline brick placement + fix-up (probe_bake tool). */
    rc->gi_brick_coarse = 9.0f;
    rc->gi_brick_levels = 3;
    rc->gi_brick_fill = 1;
    rc->gi_brick_buried = 0.5f;
    rc->gi_fixup_clearance = 0.10f;
    rc->gi_fixup_max_push = 0.60f;
    rc->gi_ray_clamp = 4.0f;
    /* rpg-96ia: low-end-safe engine defaults. 4xMSAA + 16x aniso are a heavy
     * iGPU fill/fetch cost; levels that want them pin their own value (the
     * discrete great_hall render.json now sets msaa 4 / aniso 16 explicitly). */
    rc->aniso = 4.0f; rc->msaa = 2;
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

    /* Low-end performance knobs (rpg-vwyk / rpg-iplq). Every value below
     * REPRODUCES today's behavior so a configless level is unchanged; presets
     * and per-level overrides opt into cheaper rendering. */
    rc->render_scale = 1.0f;            /* native resolution. */
    rc->pbr_quality = 2;                /* full ubershader. */
    rc->texture_quality = 0;            /* no mips dropped. */
    rc->depth_prepass = -1;             /* auto: on when >1 renderable, as today. */

    rc->shadow_fp16 = 0;                /* R32F shadow maps, as today. */
    rc->shadow_update_interval = 0;     /* all cube-shadow slots every frame. */
    rc->shadow_distance = 0.0f;         /* unlimited. */
    rc->shadow_static_cache = 0;        /* re-render every frame, as today. */
    rc->dir_pcf_taps = 8;               /* CSM 8-tap PCF, as today. */
    rc->shadow_pcf_taps = 8;            /* point-shadow 8-tap, as today. */
    rc->dir_dynamic_interval = 1;       /* dynamic CSM every frame. */

    rc->lightmap_bands = 9;             /* full SH9, as today. */
    rc->lm_fp16 = 0;                    /* RGB32F SH atlases, as today. */
    rc->lm_resident_layers = 8;         /* CLIENT_LM_MAX_RESIDENT. */

    rc->gi_dyn_voxel = 2;               /* voxelize every frame, as today. */
    rc->gi_march_quality = 1.0f;        /* full march. */
    rc->gi_frag_quality = 1;            /* full 8-corner probe gather. */
    rc->gi_prepass_scale = 8;           /* w/8 visibility prepass, as today. */
    rc->gi_probe_cap = 0;               /* unlimited probes per tick. */
    rc->gi_adaptive_ms = 0.0f;          /* never skip a GI tick. */
    rc->sdf_fp16 = 0;                   /* RGBA32F SDF chunks, as today. */
    rc->sdf_resident_slots = 8;         /* as today. */
    rc->sdf_uploads_per_frame = 0;      /* uncapped, as today. */

    rc->stream_upload_mb_per_frame = 0; /* uncapped, as today. */
    rc->stream_ram_budget_mb = 2048;    /* demo_client's hardcoded 2 GiB. */
    rc->stream_vram_budget_mb = 2048;

    rc->draw_distance = 0.0f;           /* unlimited. */
}
