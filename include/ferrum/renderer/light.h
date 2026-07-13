#ifndef FERRUM_RENDERER_LIGHT_H
#define FERRUM_RENDERER_LIGHT_H

#include <stdint.h>

/** @file
 * @brief First-class light entities for the renderer scene.
 *
 * A light carries its kind, transform, colour+intensity, range/radius, spot
 * cone, and flags marking whether it feeds the realtime pipeline, the offline
 * lightmap bake, or both, and whether it casts shadows. The point/directional/
 * spot kinds and their fields mirror the baker's lm_light_t (position,
 * direction, colour, range, cos_inner/outer) so baked and realtime lighting
 * share one definition; the extra AREA kind and radius/intensity/flags are for
 * the pipeline and area/soft-shadow use.
 *
 * Ownership: plain value type. The @ref render_light_kind values match the PBR
 * shader's u_light_type encoding (0=point, 1=directional, 2=spot).
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Light kind (0/1/2 match the PBR shader's u_light_type encoding). */
typedef enum render_light_kind {
    RENDER_LIGHT_POINT = 0,     /**< omni point (inverse-square). */
    RENDER_LIGHT_DIRECTIONAL,   /**< parallel rays, no falloff. */
    RENDER_LIGHT_SPOT,          /**< point restricted to a cone. */
    RENDER_LIGHT_AREA           /**< area emitter (bake / soft shadows). */
} render_light_kind_t;

/** This light contributes to the realtime forward+/deferred passes. */
#define RENDER_LIGHT_FLAG_REALTIME 0x1u
/** This light contributes to the offline lightmap bake. */
#define RENDER_LIGHT_FLAG_BAKED    0x2u
/** This light casts shadows. */
#define RENDER_LIGHT_FLAG_SHADOW   0x4u

/** A scene light. */
typedef struct render_light {
    render_light_kind_t kind;
    float position[3];   /**< world position (point/spot/area). */
    float direction[3];  /**< forward emission axis (directional/spot/area). */
    float color[3];      /**< linear colour (multiplied by intensity). */
    float intensity;     /**< scalar radiant-intensity multiplier. */
    float range;         /**< point/spot cutoff distance (<= 0: unbounded). */
    float radius;        /**< emitter radius (area / soft-shadow penumbra). */
    float cos_inner;     /**< spot inner (full-bright) cone cosine. */
    float cos_outer;     /**< spot outer (zero) cone cosine. */
    uint32_t flags;      /**< RENDER_LIGHT_FLAG_* bitset. */
} render_light_t;

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_RENDERER_LIGHT_H */
