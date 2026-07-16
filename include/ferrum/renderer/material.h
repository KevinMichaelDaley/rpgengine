#ifndef FERRUM_RENDERER_MATERIAL_H
#define FERRUM_RENDERER_MATERIAL_H

#include <stdint.h>

#include "ferrum/renderer/shader_program.h"
#include "ferrum/renderer/shader_uniforms.h"
#include "ferrum/renderer/texture.h"

/** @file
 * @brief PBR material: a set of texture maps + scalar parameters, and the
 *        binding of both to a shader for a draw (metallic-roughness workflow).
 *
 * A material references (does not own) up to @ref MATERIAL_TEX_COUNT texture
 * maps and carries the scalar parameters the PBR shader needs. @ref
 * material_bind binds each present map to a texture unit and uploads the
 * sampler unit + a presence flag + all scalar parameters through a uniform
 * cache, following a fixed uniform-name contract so the shader and material
 * agree:
 *
 *   samplers: u_albedo_map u_normal_map u_metallic_map u_roughness_map
 *             u_ao_map u_emissive_map u_lightmap
 *   presence: u_has_albedo u_has_normal u_has_metallic u_has_roughness
 *             u_has_ao u_has_emissive u_has_lightmap   (int 0/1)
 *   params:   u_tint (vec3) u_emissive_color (vec3)
 *             u_specular_strength u_metalness u_roughness_min u_roughness_max
 *             u_emissive_strength u_normal_scale u_ao_strength      (float)
 *             u_uv_scale (vec2)  material-texture UV tiling
 *
 * Ownership: texture maps are borrowed. Nullability: absent maps are NULL (the
 * presence flag is set to 0). Missing uniforms are tolerated (best effort) so a
 * shader may consume only a subset.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** PBR texture slots (also the default unit offsets used by material_bind). */
typedef enum material_texture_slot {
    MATERIAL_TEX_ALBEDO = 0,    /**< base colour (sRGB). */
    MATERIAL_TEX_NORMAL = 1,    /**< tangent-space normal (linear). */
    MATERIAL_TEX_METALLIC = 2,  /**< metalness (linear). */
    MATERIAL_TEX_ROUGHNESS = 3, /**< roughness (linear). */
    MATERIAL_TEX_AO = 4,        /**< ambient-occlusion "dirtmap" (linear). */
    MATERIAL_TEX_EMISSIVE = 5,  /**< emissive self-shading (sRGB). */
    MATERIAL_TEX_LIGHTMAP = 6,  /**< baked SH-irradiance atlas (float). */
    MATERIAL_TEX_COUNT = 7
} material_texture_slot_t;

/** A PBR material: borrowed texture maps + scalar parameters. */
typedef struct render_material {
    const texture_t *maps[MATERIAL_TEX_COUNT]; /**< borrowed; NULL if absent. */
    float tint[3];            /**< base-colour multiplier (linear). */
    float specular_strength;  /**< dielectric specular scale (0..1, ~0.5). */
    float metalness;          /**< scalar metalness / metal-map multiplier. */
    float roughness_min;      /**< remap: roughness map 0 -> this. */
    float roughness_max;      /**< remap: roughness map 1 -> this. */
    float emissive_color[3];  /**< self-shading emissive colour (linear). */
    float emissive_strength;  /**< emissive self-shading scale. */
    float normal_scale;       /**< tangent-normal XY strength. */
    float ao_strength;        /**< AO map influence (0..1). */
    float uv_scale[2];        /**< material-texture UV tiling (multiplies v_uv0). */
    float contrast;           /**< albedo contrast about 0.5 (1 = none, >1 punchier). */
} render_material_t;

/**
 * @brief Initialise @p mat to neutral defaults (white tint, specular 0.5,
 *        metalness 0, roughness 0..1, strengths 1, all maps NULL).
 */
void material_init(render_material_t *mat);

/**
 * @brief Bind the material for a draw: bind each present map to
 *        (@p base_unit + slot), and upload sampler units, presence flags, and
 *        all scalar params through @p cache / @p program (see the name contract
 *        above). Missing uniforms are ignored.
 * @return The number of texture maps actually bound.
 */
uint32_t material_bind(const render_material_t *mat, uint32_t base_unit,
                       shader_uniform_cache_t *cache,
                       const shader_program_t *program);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_RENDERER_MATERIAL_H */
