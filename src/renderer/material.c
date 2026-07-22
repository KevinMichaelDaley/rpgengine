#include "ferrum/renderer/material.h"

#include <stddef.h>

/* Uniform-name contract, indexed by material_texture_slot_t. */
static const char *const MATERIAL_SAMPLER_NAME[MATERIAL_TEX_COUNT] = {
    "u_albedo_map",  "u_normal_map",   "u_metallic_map", "u_roughness_map",
    "u_ao_map",      "u_emissive_map", "u_lightmap",
};
static const char *const MATERIAL_HAS_NAME[MATERIAL_TEX_COUNT] = {
    "u_has_albedo", "u_has_normal",   "u_has_metallic", "u_has_roughness",
    "u_has_ao",     "u_has_emissive", "u_has_lightmap",
};

void material_init(render_material_t *mat)
{
    if (mat == NULL) {
        return;
    }
    for (int i = 0; i < MATERIAL_TEX_COUNT; ++i) {
        mat->maps[i] = NULL;
    }
    mat->tint[0] = mat->tint[1] = mat->tint[2] = 1.0f;
    mat->specular_strength = 0.5f;
    mat->metalness = 0.0f;
    mat->roughness_min = 0.0f;
    mat->roughness_max = 1.0f;
    mat->emissive_color[0] = mat->emissive_color[1] = mat->emissive_color[2] = 0.0f;
    mat->emissive_strength = 1.0f;
    mat->normal_scale = 1.0f;
    mat->ao_strength = 1.0f;
    mat->uv_scale[0] = mat->uv_scale[1] = 1.0f;
    mat->contrast = 1.0f;
    mat->opacity = 1.0f;
    mat->orm_packed = 0;
}

uint32_t material_bind(const render_material_t *mat, uint32_t base_unit,
                       shader_uniform_cache_t *cache,
                       const shader_program_t *program)
{
    if (mat == NULL || cache == NULL || program == NULL) {
        return 0u;
    }
    uint32_t bound = 0u;
    for (int slot = 0; slot < MATERIAL_TEX_COUNT; ++slot) {
        const texture_t *map = mat->maps[slot];
        if (map != NULL) {
            uint32_t unit = base_unit + (uint32_t)slot;
            texture_bind(map, unit);
            /* Best effort: the shader may not declare every sampler. */
            shader_uniform_set_int(cache, program, MATERIAL_SAMPLER_NAME[slot],
                                   (int32_t)unit);
            shader_uniform_set_int(cache, program, MATERIAL_HAS_NAME[slot], 1);
            ++bound;
        } else {
            shader_uniform_set_int(cache, program, MATERIAL_HAS_NAME[slot], 0);
        }
    }

    shader_uniform_set_vec3(cache, program, "u_tint", mat->tint);
    shader_uniform_set_float(cache, program, "u_contrast",
                             mat->contrast > 0.0f ? mat->contrast : 1.0f);
    shader_uniform_set_vec3(cache, program, "u_emissive_color", mat->emissive_color);
    shader_uniform_set_float(cache, program, "u_specular_strength",
                             mat->specular_strength);
    shader_uniform_set_float(cache, program, "u_metalness", mat->metalness);
    shader_uniform_set_float(cache, program, "u_roughness_min",
                             mat->roughness_min);
    shader_uniform_set_float(cache, program, "u_roughness_max",
                             mat->roughness_max);
    shader_uniform_set_int(cache, program, "u_orm_packed", mat->orm_packed);
    shader_uniform_set_float(cache, program, "u_opacity", mat->opacity);
    shader_uniform_set_float(cache, program, "u_emissive_strength",
                             mat->emissive_strength);
    shader_uniform_set_float(cache, program, "u_normal_scale", mat->normal_scale);
    shader_uniform_set_float(cache, program, "u_ao_strength", mat->ao_strength);
    shader_uniform_set_vec2(cache, program, "u_uv_scale", mat->uv_scale);
    return bound;
}
