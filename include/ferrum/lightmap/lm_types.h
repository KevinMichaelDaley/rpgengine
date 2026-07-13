/**
 * @file lm_types.h
 * @brief Core value types for the lightmap baker: a luxel and a bakeable
 *        surface (an axis-free quad patch).
 *
 * A surface is a parallelogram patch (origin + two edge vectors) diced into a
 * res_u x res_v grid of luxels. Each luxel stores its world position + normal,
 * the surface's diffuse albedo (for bounces) and emissive radiance (a static
 * area emitter), and the accumulated incident-radiance L2 SH per RGB channel
 * that the solver fills. Real meshes feed in as one surface per (roughly
 * planar) chart via their uv1 lightmap coords; this quad form keeps the early
 * modules testable without the full mesh pipeline.
 *
 * POD; no ownership. All helpers are pure. Directions/positions are world space.
 */
#ifndef FERRUM_LIGHTMAP_LM_TYPES_H
#define FERRUM_LIGHTMAP_LM_TYPES_H

#include <math.h>
#include <stdint.h>

#include "ferrum/lightmap/lm_sh.h"
#include "ferrum/math/vec3.h"

#ifdef __cplusplus
extern "C" {
#endif

/** One lightmap texel: geometry + material + accumulated directional light. */
typedef struct lm_luxel {
    vec3_t   pos;      /**< World position (patch centre of this texel). */
    vec3_t   normal;   /**< Unit surface normal. */
    vec3_t   albedo;   /**< Diffuse reflectance (0..1 per channel). */
    vec3_t   emissive; /**< Self-emitted radiance (per channel). */
    lm_sh9_t sh[3];    /**< Incident-radiance SH per RGB channel. */
} lm_luxel_t;

/** A parallelogram surface patch diced into a luxel grid. */
typedef struct lm_surface {
    vec3_t   origin;   /**< A corner of the patch. */
    vec3_t   edge_u;   /**< Edge vector to the neighbouring corner (u axis). */
    vec3_t   edge_v;   /**< Edge vector to the other neighbour (v axis). */
    vec3_t   normal;   /**< Unit outward normal. */
    vec3_t   albedo;   /**< Diffuse reflectance. */
    vec3_t   emissive; /**< Emitted radiance (0 = not an emitter). */
    uint32_t res_u;    /**< Luxel columns along u. */
    uint32_t res_v;    /**< Luxel rows along v. */
} lm_surface_t;

/** World position of the centre of luxel (iu, iv) on the surface. */
static inline vec3_t lm_surface_point(const lm_surface_t *s, uint32_t iu,
                                      uint32_t iv)
{
    float fu = ((float)iu + 0.5f) / (float)s->res_u;
    float fv = ((float)iv + 0.5f) / (float)s->res_v;
    return vec3_add(s->origin,
                    vec3_add(vec3_scale(s->edge_u, fu),
                             vec3_scale(s->edge_v, fv)));
}

/** Total world area of the surface patch (|edge_u x edge_v|). */
static inline float lm_surface_area(const lm_surface_t *s)
{
    return vec3_magnitude(vec3_cross(s->edge_u, s->edge_v));
}

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_LIGHTMAP_LM_TYPES_H */
