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
    return true;
}
