/**
 * @file scene_desc_light.h
 * @brief Descriptor schema for a level's discrete light set (rpg-8302).
 *
 * The exporter (Blender / editor, scripts/export_scene.py) emits every light in
 * the level here -- including the sun, a directional light flagged
 * ::SCENE_DESC_LIGHT_FLAG_BAKED so it is folded into the offline lightmap bake.
 * This is pure headless data (no GL, no renderer dependency): the server's
 * headless GI and the client's render-world builder BOTH read it, each
 * translating a ::scene_desc_light_t into their own light type
 * (render_light_t / lm_light_t). The fields and flag bit-values mirror the
 * renderer's render_light_t / RENDER_LIGHT_FLAG_* one-for-one so translation is
 * a straight field copy; keep them in sync if either changes.
 *
 * Ownership: plain value type; a level's light array is arena-allocated by the
 * loader (valid for the arena's lifetime). Not thread-safe.
 */
#ifndef FERRUM_SCENE_SCENE_DESC_LIGHT_H
#define FERRUM_SCENE_SCENE_DESC_LIGHT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "ferrum/scene/scene_desc_object.h" /* SCENE_DESC_OBJ_NAME_CAP */

/**
 * @brief Light kind. Values match the PBR shader's u_light_type encoding and
 *        the renderer's render_light_kind_t (0=point, 1=directional, 2=spot).
 */
typedef enum scene_desc_light_kind {
    SCENE_DESC_LIGHT_POINT = 0,   /**< omni point (inverse-square falloff). */
    SCENE_DESC_LIGHT_DIRECTIONAL, /**< parallel rays, no falloff (e.g. the sun). */
    SCENE_DESC_LIGHT_SPOT,        /**< point restricted to a cone. */
    SCENE_DESC_LIGHT_AREA         /**< area emitter (bake / soft shadows). */
} scene_desc_light_kind_t;

/* Flag bit-values mirror RENDER_LIGHT_FLAG_* exactly (renderer/light.h). */
/** Contributes to the realtime forward+ pass. */
#define SCENE_DESC_LIGHT_FLAG_REALTIME         0x1u
/** Folded into the offline lightmap bake (the sun carries this). */
#define SCENE_DESC_LIGHT_FLAG_BAKED            0x2u
/** Casts shadows. */
#define SCENE_DESC_LIGHT_FLAG_SHADOW           0x4u
/** Gathered by the dynamic SDF-probe GI for the indirect term. */
#define SCENE_DESC_LIGHT_FLAG_DYNAMIC_INDIRECT 0x8u
/** Probe GI traces indirect from ONLY lights carrying this flag. */
#define SCENE_DESC_LIGHT_FLAG_PROBE_GI         0x10u

/**
 * @brief A single level light. Mirrors render_light_t field-for-field.
 *
 * @c cos_inner / @c cos_outer store the spot cone as cosines (the exporter's
 * degree cones are converted at parse time). For non-spot kinds they are
 * ignored. @c range <= 0 means unbounded (directional / infinite point).
 */
typedef struct scene_desc_light {
    char                    name[SCENE_DESC_OBJ_NAME_CAP]; /**< editor id (may be empty). */
    scene_desc_light_kind_t kind;
    float                   position[3];  /**< world position (point/spot/area). */
    float                   direction[3]; /**< emission axis (directional/spot). */
    float                   color[3];     /**< linear colour (x intensity). */
    float                   intensity;    /**< radiant-intensity multiplier. */
    float                   range;        /**< point/spot cutoff (<=0 unbounded). */
    float                   radius;       /**< emitter radius (area/penumbra). */
    float                   cos_inner;    /**< spot inner (full-bright) cone cosine. */
    float                   cos_outer;    /**< spot outer (zero) cone cosine. */
    uint32_t                flags;        /**< SCENE_DESC_LIGHT_FLAG_* bitset. */
} scene_desc_light_t;

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_SCENE_SCENE_DESC_LIGHT_H */
