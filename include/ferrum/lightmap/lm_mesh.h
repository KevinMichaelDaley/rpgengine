/**
 * @file lm_mesh.h
 * @brief Triangle-mesh inputs for the lightmap baker.
 *
 * Where lm_surface describes a single lightmappable quad, lm_mesh describes an
 * arbitrary indexed triangle mesh with a per-vertex LIGHTMAP UV (uv1, laid out
 * in [0,1] by an external unwrap -- e.g. Blender's "Lightmap UVs" tool). The
 * baker packs each mesh's [0,1] lightmap layout into an atlas region sized by
 * @c lightmap_resolution and generates one luxel per covered atlas texel, with
 * world position and normal barycentric-interpolated from the covering triangle.
 *
 * Ownership: all arrays are borrowed. Nullability: pointers non-NULL when the
 * corresponding count > 0. uv1 values are expected in [0,1].
 */
#ifndef FERRUM_LIGHTMAP_LM_MESH_H
#define FERRUM_LIGHTMAP_LM_MESH_H

#include <stdint.h>

#include "ferrum/lightmap/lm_image.h"
#include "ferrum/lightmap/lm_light.h"
#include "ferrum/lightmap/lm_material.h"
#include "ferrum/math/vec3.h"

#ifdef __cplusplus
extern "C" {
#endif

/** An indexed triangle mesh to bake, with material + lightmap UVs. */
typedef struct lm_mesh {
    const float    *positions; /**< vec3 * vert_count (world space). */
    const float    *normals;   /**< vec3 * vert_count. */
    const float    *uv0;       /**< vec2 * vert_count, MATERIAL UV (sampling). */
    const float    *uv1;       /**< vec2 * vert_count, lightmap UV in [0,1]. */
    const uint32_t *indices;   /**< index_count triangle indices. */
    uint32_t        vert_count;
    uint32_t        index_count;
    /* Diffuse reflectance + emissive come from the material TEXTURES sampled by
     * uv0; albedo/emissive act as a tint/scale (default 1). If an image is NULL
     * the tint value is used directly (flat). */
    const lm_image_t *albedo_image;   /**< diffuse reflectance texture (nullable). */
    const lm_image_t *emissive_image; /**< emissive texture (nullable). */
    vec3_t          albedo;    /**< tint/fallback reflectance. */
    vec3_t          emissive;  /**< tint/fallback emission. */
    float           opacity;   /**< 1 = opaque (default); <1 = translucent (glass).
                                *   Stamped into the SDF transmission channel so probe
                                *   rays can pass through windows into interiors. */
    uint16_t        material;  /**< SVO material id (for far-field reflectors). */
    uint32_t        lightmap_resolution; /**< atlas rect size in texels. */
} lm_mesh_t;

/** A scene of triangle meshes + lights for the mesh baker. */
typedef struct lm_mesh_scene {
    const lm_mesh_t    *meshes;
    uint32_t            n_meshes;
    const lm_light_t   *lights;   /**< analytic (indirect) lights. */
    uint32_t            n_lights;
    lm_material_table_t materials;/**< id -> albedo/emissive for far field. */
} lm_mesh_scene_t;

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_LIGHTMAP_LM_MESH_H */
