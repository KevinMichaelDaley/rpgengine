/**
 * @file shadow_spot.h
 * @brief Spot-light (2D) shadow map: an R32F depth-distance map rendered by a
 *        single perspective projection down a spot light's cone. Stores linear
 *        light->fragment distance / far; the PBR shader projects the fragment
 *        through the light-space matrix, samples, and PCF-compares. The 2D
 *        analogue of @ref shadow_cube for cone (and directional-ish) lights.
 *
 * Ownership: owns its texture/renderbuffer/FBO/shader; frees them in
 * @ref shadow_spot_destroy. Per frame, bake-free.
 */
#ifndef FERRUM_RENDERER_SHADOW_SPOT_H
#define FERRUM_RENDERER_SHADOW_SPOT_H

#include <stdint.h>

#include "ferrum/math/mat4.h"
#include "ferrum/renderer/gl_loader.h"
#include "ferrum/renderer/render_scene.h"
#include "ferrum/renderer/shader_program.h"
#include "ferrum/renderer/shader_uniforms.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Spot 2D shadow map + its GL resources. */
typedef struct shadow_spot {
    shader_program_t       shader;
    shader_uniform_cache_t cache;
    uint32_t resolution;
    float    near_plane;
    float    far_plane;
    mat4_t   view_proj;              /**< last render's light-space matrix. */
    uint32_t map;                    /**< R32F 2D depth-distance. */
    uint32_t depth_rb;
    uint32_t fbo;

    void (*glGenFramebuffers)(int32_t, uint32_t *);
    void (*glDeleteFramebuffers)(int32_t, const uint32_t *);
    void (*glBindFramebuffer)(uint32_t, uint32_t);
    void (*glFramebufferTexture2D)(uint32_t, uint32_t, uint32_t, uint32_t, int32_t);
    void (*glGenRenderbuffers)(int32_t, uint32_t *);
    void (*glDeleteRenderbuffers)(int32_t, const uint32_t *);
    void (*glBindRenderbuffer)(uint32_t, uint32_t);
    void (*glRenderbufferStorage)(uint32_t, uint32_t, int32_t, int32_t);
    void (*glFramebufferRenderbuffer)(uint32_t, uint32_t, uint32_t, uint32_t);
    void (*glGenTextures)(int32_t, uint32_t *);
    void (*glDeleteTextures)(int32_t, const uint32_t *);
    void (*glBindTexture)(uint32_t, uint32_t);
    void (*glActiveTexture)(uint32_t);
    void (*glTexImage2D)(uint32_t, int32_t, int32_t, int32_t, int32_t, int32_t, uint32_t, uint32_t, const void *);
    void (*glTexParameteri)(uint32_t, uint32_t, int32_t);
    void (*glViewport)(int32_t, int32_t, int32_t, int32_t);
    void (*glClearColor)(float, float, float, float);
    void (*glClear)(uint32_t);
    void (*glEnable)(uint32_t);
    void (*glDepthFunc)(uint32_t);
} shadow_spot_t;

/** @brief Create the 2D map (R32F, @p resolution), depth RB, FBO, distance shader. */
bool shadow_spot_init(shadow_spot_t *ss, const gl_loader_t *loader,
                      uint32_t resolution, float near_plane, float far_plane);

/**
 * @brief Render the scene depth-distance from @p light_pos looking along
 *        @p light_dir with full cone angle @p fov_radians into the 2D map, and
 *        store the light-space matrix. Caller re-binds its framebuffer/viewport.
 */
void shadow_spot_render(shadow_spot_t *ss, const render_scene_t *scene,
                        const float light_pos[3], const float light_dir[3],
                        float fov_radians);

/** @brief Bind the map to @p unit and set u_spot_map / u_spot_vp / u_spot_far. */
void shadow_spot_bind(const shadow_spot_t *ss, shader_uniform_cache_t *cache,
                      const shader_program_t *program, uint32_t unit);

/** @brief Release all GL resources. NULL-safe. */
void shadow_spot_destroy(shadow_spot_t *ss);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_RENDERER_SHADOW_SPOT_H */
