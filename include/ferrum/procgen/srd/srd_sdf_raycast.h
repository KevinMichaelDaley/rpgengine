/**
 * @file srd_sdf_raycast.h
 * @brief CPU SDF raymarcher for training-time image generation.
 *
 * Sphere-traces the SDF grid directly, computing smooth surface
 * normals from SDF gradients via central differences. Renders with
 * sphere-traced soft shadows, SDF ambient occlusion, and Blinn-Phong
 * lighting from multiple point lights.
 *
 * Types (2): srd_point_light_t, srd_raycast_config_t
 * Non-static functions declared (3): srd_raycast_config_default,
 *                                     srd_sdf_raycast, srd_sdf_sample
 */
#ifndef SRD_SDF_RAYCAST_H
#define SRD_SDF_RAYCAST_H

#include "ferrum/procgen/srd/srd_sdf_grid.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Maximum number of point lights per render pass. */
#define SRD_MAX_LIGHTS 8

/**
 * @brief A point light with position, color, and power.
 *
 * Physical inverse-square falloff: atten = radius / (d² + 1).
 * The radius field acts as luminous power (not a distance).
 * At d=1m with radius=1, atten≈0.5. At d=3m, atten≈0.1.
 */
typedef struct {
    float pos[3];       /**< World-space position. */
    float color[3];     /**< RGB tint (typically 0..1, pre-multiplied by power). */
    float radius;       /**< Luminous power (scales inverse-square falloff). */
} srd_point_light_t;

/**
 * @brief Configuration for a single raycast render pass.
 */
typedef struct {
    float cam_pos[3];    /**< Eye position in world space. */
    float cam_dir[3];    /**< Look direction (normalized). */
    float cam_up[3];     /**< Up vector (normalized, not parallel to cam_dir). */
    float fov_y;         /**< Vertical field of view in radians. */
    int   width;         /**< Image width in pixels. */
    int   height;        /**< Image height in pixels. */
    float light_dir[3];  /**< (Unused — kept for ABI compat. Use point lights.) */
    float ambient;       /**< Ambient light intensity [0, 1]. */
    int   max_steps;     /**< Maximum sphere-trace steps per ray. */
    float hit_epsilon;   /**< Surface hit threshold (SDF < epsilon). */
    srd_point_light_t lights[SRD_MAX_LIGHTS]; /**< Point lights. */
    int   n_lights;      /**< Number of active lights (0..SRD_MAX_LIGHTS). */
} srd_raycast_config_t;

/**
 * @brief Fill a config with sensible defaults.
 *
 * Defaults: 128x128, 60deg fov, camera at (-5,2,-5) looking at origin,
 * no lights (add them yourself), ambient 0.15, 256 steps, epsilon 0.001.
 *
 * @param cfg  Config to populate. Must not be NULL.
 */
void srd_raycast_config_default(srd_raycast_config_t *cfg);

/**
 * @brief Render the SDF grid to an RGB image via sphere tracing.
 *
 * For each pixel, casts a ray through the SDF grid. On hit, computes
 * the surface normal from SDF gradients (central differences with
 * trilinear interpolation), then for each point light, sphere-traces
 * a shadow ray and accumulates diffuse+specular contribution.
 *
 * Missed rays produce a dark background. The output is interleaved
 * RGB (3 bytes per pixel, row-major, top-to-bottom).
 *
 * @param grid    SDF grid to render. NULL -> all pixels are background.
 * @param cfg     Render configuration. NULL -> no-op.
 * @param rgb_out Output buffer. Must hold width * height * 3 bytes.
 *                NULL -> no-op.
 *
 * @note Ownership: rgb_out is caller-owned. grid and cfg are read-only.
 * @note Side effects: none (pure computation).
 * @note Thread safety: safe to call from multiple threads with
 *       independent output buffers on the same grid.
 */
void srd_sdf_raycast(const srd_sdf_grid_t *grid,
                     const srd_raycast_config_t *cfg,
                     uint8_t *rgb_out);

/**
 * @brief Trilinear-interpolated SDF sample at a world-space position.
 *
 * Returns the smoothly interpolated SDF value. Out-of-bounds
 * positions return SRD_SDF_OUTSIDE.
 *
 * @param grid  SDF grid to sample. NULL -> returns SRD_SDF_OUTSIDE.
 * @param wx,wy,wz  World-space position.
 * @return Interpolated SDF value.
 */
float srd_sdf_sample(const srd_sdf_grid_t *grid,
                     float wx, float wy, float wz);

#ifdef __cplusplus
}
#endif

#endif /* SRD_SDF_RAYCAST_H */
