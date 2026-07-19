/**
 * @file scene_desc_material.h
 * @brief A material definition in a scene descriptor (rpg-8302).
 *
 * Full PBR material: texture asset paths + parameters, so the client render-world
 * can build a render_material_t without hardcoding per-level material tuning (as
 * hall_lit_dynamic.c does today). A descriptor "materials" entry may be a bare
 * string (name only, engine defaults) or an object with these fields. Pure data.
 */
#ifndef FERRUM_SCENE_SCENE_DESC_MATERIAL_H
#define FERRUM_SCENE_SCENE_DESC_MATERIAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ferrum/scene/scene_desc_object.h" /* SCENE_DESC_PATH_CAP */

/** Texture slots (match render material MATERIAL_TEX_ALBEDO..EMISSIVE order). */
enum {
    SCENE_DESC_MAT_TEX_ALBEDO = 0,
    SCENE_DESC_MAT_TEX_NORMAL,
    SCENE_DESC_MAT_TEX_METALLIC,
    SCENE_DESC_MAT_TEX_ROUGHNESS,
    SCENE_DESC_MAT_TEX_AO,
    SCENE_DESC_MAT_TEX_EMISSIVE,
    SCENE_DESC_MAT_TEX_COUNT
};

/** Material name capacity (incl. null terminator). */
#define SCENE_DESC_MATERIAL_NAME_CAP 64u

/**
 * @brief One PBR material definition. Empty texture paths mean "no map". Scalar
 * defaults (set by the parser) are engine-neutral: tint 1, roughness_max 1,
 * normal_scale 1, uv_scale 1, contrast 1, ao_strength 1, the rest 0.
 */
typedef struct scene_desc_material {
    char  name[SCENE_DESC_MATERIAL_NAME_CAP];
    char  tex[SCENE_DESC_MAT_TEX_COUNT][SCENE_DESC_PATH_CAP]; /**< per-slot asset path. */
    float tint[3];
    float metalness;
    float roughness_min, roughness_max;
    float normal_scale;
    float uv_scale[2];
    float contrast;
    float ao_strength;
    float emissive_color[3];
    float emissive_strength;
    int   orm_packed;          /**< 1 = roughness map is packed ORM (R=ao,G=roughness). */
} scene_desc_material_t;

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_SCENE_SCENE_DESC_MATERIAL_H */
