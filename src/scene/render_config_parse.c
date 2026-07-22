/**
 * @file render_config_parse.c
 * @brief JSON overlay parser for render_config (rpg-da8c): start from the engine
 *        defaults, then override every key PRESENT in the document (missing keys
 *        keep their default). Reuses the scene-descriptor JSON field helpers.
 */
#include <string.h>

#include "ferrum/memory/arena.h"
#include "ferrum/scene/render_config.h"
#include "scene_desc_internal.h"   /* sd_field_num / _vec + json_parse */

/* Read an unsigned field via the numeric helper (keeps @p cur if absent). */
static uint32_t rc_field_u(const json_value_t *o, const char *k, uint32_t cur)
{
    return (uint32_t)sd_field_num(o, k, (float)cur);
}
/* Read a signed/int field via the numeric helper (keeps @p cur if absent). */
static int rc_field_i(const json_value_t *o, const char *k, int cur)
{
    return (int)sd_field_num(o, k, (float)cur);
}

bool render_config_parse(const char *json, size_t len, struct arena *arena,
                         render_config_t *out)
{
    if (json == NULL || arena == NULL || out == NULL) return false;
    render_config_defaults(out);

    /* Carve the JSON node tree from the caller arena (no malloc). */
    size_t jsize = len * 12u + 4096u;
    void *jbuf = arena_alloc((arena_t *)arena, 16u, jsize);
    if (jbuf == NULL) return false;
    json_arena_t ja;
    json_arena_init(&ja, jbuf, jsize);
    json_value_t root;
    if (!json_parse(json, len, &ja, &root) || root.type != JSON_OBJECT) return false;
    const json_value_t *r = &root;

    /* Forward+ cluster. */
    out->cluster_tiles_x = rc_field_u(r, "cluster_tiles_x", out->cluster_tiles_x);
    out->cluster_tiles_y = rc_field_u(r, "cluster_tiles_y", out->cluster_tiles_y);
    out->cluster_slices  = rc_field_u(r, "cluster_slices",  out->cluster_slices);
    out->cluster_near    = sd_field_num(r, "cluster_near",  out->cluster_near);
    out->cluster_far     = sd_field_num(r, "cluster_far",   out->cluster_far);
    out->max_lights      = rc_field_u(r, "max_lights",      out->max_lights);

    /* Lightmap. */
    out->sh_enabled      = rc_field_i(r, "sh_enabled",        out->sh_enabled);
    out->sh_scale        = sd_field_num(r, "sh_scale",        out->sh_scale);
    out->sh_normal_bias  = sd_field_num(r, "sh_normal_bias",  out->sh_normal_bias);
    sd_field_vec(r, "ambient", out->ambient, 3);

    /* Sun + CSM. */
    out->sun_energy_scale = sd_field_num(r, "sun_energy_scale", out->sun_energy_scale);
    out->dir_cascades     = rc_field_u(r, "dir_cascades",     out->dir_cascades);
    out->dir_static_res   = rc_field_u(r, "dir_static_res",   out->dir_static_res);
    out->dir_dynamic_res  = rc_field_u(r, "dir_dynamic_res",  out->dir_dynamic_res);
    out->dir_lambda       = sd_field_num(r, "dir_lambda",     out->dir_lambda);
    out->dir_bias         = sd_field_num(r, "dir_bias",       out->dir_bias);
    out->dir_softness     = sd_field_num(r, "dir_softness",   out->dir_softness);
    out->dir_max_distance = sd_field_num(r, "dir_max_distance", out->dir_max_distance);
    out->dir_pcss         = rc_field_i(r, "dir_pcss",         out->dir_pcss);
    out->dir_translucency = rc_field_i(r, "dir_translucency", out->dir_translucency);
    out->dir_caustics     = rc_field_i(r, "dir_caustics",     out->dir_caustics);

    /* Point (cube) shadows. */
    out->shadow_max       = rc_field_u(r, "shadow_max",       out->shadow_max);
    out->shadow_res       = rc_field_u(r, "shadow_res",       out->shadow_res);
    out->shadow_near      = sd_field_num(r, "shadow_near",    out->shadow_near);
    out->shadow_far_scale = sd_field_num(r, "shadow_far_scale", out->shadow_far_scale);
    out->shadow_bias      = sd_field_num(r, "shadow_bias",    out->shadow_bias);

    /* GI runtime. */
    out->gi_enabled       = rc_field_i(r, "gi_enabled",       out->gi_enabled);
    out->gi_grid_cell     = sd_field_num(r, "gi_grid_cell",   out->gi_grid_cell);
    out->gi_soft_k        = sd_field_num(r, "gi_soft_k",      out->gi_soft_k);
    out->gi_max_lights    = rc_field_u(r, "gi_max_lights",    out->gi_max_lights);
    out->gi_max_boxes     = rc_field_u(r, "gi_max_boxes",     out->gi_max_boxes);
    out->gi_probe_min     = rc_field_u(r, "gi_probe_min",     out->gi_probe_min);
    out->gi_probe_sphere_margin = sd_field_num(r, "gi_probe_sphere_margin", out->gi_probe_sphere_margin);
    out->gi_bin_interval   = rc_field_i(r, "gi_bin_interval",   out->gi_bin_interval);
    out->gi_update_interval = rc_field_i(r, "gi_update_interval", out->gi_update_interval);
    out->gi_n_probe_groups = rc_field_i(r, "gi_n_probe_groups", out->gi_n_probe_groups);
    out->gi_smooth          = sd_field_num(r, "gi_smooth",         out->gi_smooth);
    out->gi_field       = rc_field_i(r, "gi_field",       out->gi_field);
    out->gi_mis         = rc_field_i(r, "gi_mis",         out->gi_mis);
    out->gi_hybrid      = rc_field_i(r, "gi_hybrid",      out->gi_hybrid);
    out->gi_hero        = rc_field_i(r, "gi_hero",        out->gi_hero);
    out->gi_samples     = rc_field_i(r, "gi_samples",     out->gi_samples);
    out->gi_spec_lobes  = rc_field_i(r, "gi_spec_lobes",  out->gi_spec_lobes);
    out->gi_bounce      = sd_field_num(r, "gi_bounce",     out->gi_bounce);
    out->gi_near        = sd_field_num(r, "gi_near",       out->gi_near);
    out->gi_dmax        = sd_field_num(r, "gi_dmax",       out->gi_dmax);
    out->gi_emin        = sd_field_num(r, "gi_emin",       out->gi_emin);
    out->gi_norm_gate   = sd_field_num(r, "gi_norm_gate",  out->gi_norm_gate);
    out->gi_stat_scale  = sd_field_num(r, "gi_stat_scale", out->gi_stat_scale);
    out->gi_dyn_gain    = sd_field_num(r, "gi_dyn_gain",   out->gi_dyn_gain);
    out->gi_vis_bias    = sd_field_num(r, "gi_vis_bias",   out->gi_vis_bias);
    out->gi_vis_varmin  = sd_field_num(r, "gi_vis_varmin", out->gi_vis_varmin);
    out->gi_vis_sharp   = sd_field_num(r, "gi_vis_sharp",  out->gi_vis_sharp);
    out->gi_brick_coarse    = sd_field_num(r, "gi_brick_coarse",    out->gi_brick_coarse);
    out->gi_brick_levels    = rc_field_i(r, "gi_brick_levels",      out->gi_brick_levels);
    out->gi_brick_fill      = rc_field_i(r, "gi_brick_fill",        out->gi_brick_fill);
    out->gi_brick_buried    = sd_field_num(r, "gi_brick_buried",    out->gi_brick_buried);
    out->gi_fixup_clearance = sd_field_num(r, "gi_fixup_clearance", out->gi_fixup_clearance);
    out->gi_fixup_max_push  = sd_field_num(r, "gi_fixup_max_push",  out->gi_fixup_max_push);
    out->gi_ray_clamp       = sd_field_num(r, "gi_ray_clamp",       out->gi_ray_clamp);
    out->aniso          = sd_field_num(r, "aniso",         out->aniso);
    out->msaa           = rc_field_i(r, "msaa",            out->msaa);
    out->probe_spacing_scale = sd_field_num(r, "probe_spacing_scale", out->probe_spacing_scale);
    sd_field_vec(r, "gi_aabb_pad_lo", out->gi_aabb_pad_lo, 3);
    sd_field_vec(r, "gi_aabb_pad_hi", out->gi_aabb_pad_hi, 3);

    /* Static / spec / sky-AO. */
    out->static_baked_w = sd_field_num(r, "static_baked_w", out->static_baked_w);
    out->static_dyn_w   = sd_field_num(r, "static_dyn_w",   out->static_dyn_w);
    out->static_k       = sd_field_num(r, "static_k",       out->static_k);
    out->spec_gain      = sd_field_num(r, "spec_gain",      out->spec_gain);
    sd_field_vec(r, "sky_ao_color", out->sky_ao_color, 3);
    out->sky_ao_ref     = sd_field_num(r, "sky_ao_ref",     out->sky_ao_ref);
    out->sky_ao_mult    = sd_field_num(r, "sky_ao_mult",    out->sky_ao_mult);

    /* Low-end performance knobs (rpg-vwyk / rpg-iplq). */
    out->render_scale     = sd_field_num(r, "render_scale",     out->render_scale);
    out->pbr_quality      = rc_field_i(r, "pbr_quality",        out->pbr_quality);
    out->texture_quality  = rc_field_i(r, "texture_quality",    out->texture_quality);
    out->depth_prepass    = rc_field_i(r, "depth_prepass",      out->depth_prepass);

    out->shadow_fp16            = rc_field_i(r, "shadow_fp16",            out->shadow_fp16);
    out->shadow_update_interval = rc_field_i(r, "shadow_update_interval", out->shadow_update_interval);
    out->shadow_distance        = sd_field_num(r, "shadow_distance",      out->shadow_distance);
    out->shadow_static_cache    = rc_field_i(r, "shadow_static_cache",    out->shadow_static_cache);
    out->dir_pcf_taps           = rc_field_i(r, "dir_pcf_taps",           out->dir_pcf_taps);
    out->shadow_pcf_taps        = rc_field_i(r, "shadow_pcf_taps",        out->shadow_pcf_taps);
    out->dir_dynamic_interval   = rc_field_i(r, "dir_dynamic_interval",   out->dir_dynamic_interval);

    out->lightmap_bands     = rc_field_i(r, "lightmap_bands",     out->lightmap_bands);
    out->lm_fp16            = rc_field_i(r, "lm_fp16",            out->lm_fp16);
    out->lm_resident_layers = rc_field_i(r, "lm_resident_layers", out->lm_resident_layers);

    out->gi_dyn_voxel     = rc_field_i(r, "gi_dyn_voxel",     out->gi_dyn_voxel);
    out->gi_march_quality = sd_field_num(r, "gi_march_quality", out->gi_march_quality);
    out->gi_frag_quality  = rc_field_i(r, "gi_frag_quality",  out->gi_frag_quality);
    out->gi_prepass_scale = rc_field_i(r, "gi_prepass_scale", out->gi_prepass_scale);
    out->gi_probe_cap     = rc_field_i(r, "gi_probe_cap",     out->gi_probe_cap);
    out->gi_adaptive_ms   = sd_field_num(r, "gi_adaptive_ms", out->gi_adaptive_ms);
    out->sdf_fp16         = rc_field_i(r, "sdf_fp16",         out->sdf_fp16);
    out->sdf_resident_slots    = rc_field_i(r, "sdf_resident_slots",    out->sdf_resident_slots);
    out->sdf_uploads_per_frame = rc_field_i(r, "sdf_uploads_per_frame", out->sdf_uploads_per_frame);

    out->stream_upload_mb_per_frame = rc_field_i(r, "stream_upload_mb_per_frame", out->stream_upload_mb_per_frame);
    out->stream_ram_budget_mb       = rc_field_i(r, "stream_ram_budget_mb",  out->stream_ram_budget_mb);
    out->stream_vram_budget_mb      = rc_field_i(r, "stream_vram_budget_mb", out->stream_vram_budget_mb);

    out->draw_distance    = sd_field_num(r, "draw_distance",  out->draw_distance);
    return true;
}
