/**
 * @file shadow_cube.h
 * @brief Omnidirectional (point-light) shadow map: an R32F cubemap storing the
 *        LINEAR light-to-fragment distance (normalised by the far plane) for one
 *        movable point light, rendered by projecting the scene into all six cube
 *        faces from the light position. The PBR shader samples it by the
 *        fragment->light direction and PCF-compares to shadow the light.
 *
 * Storing linear distance in a colour cubemap (rather than hardware depth) makes
 * the shadow test face-independent: a single directional lookup + distance
 * compare, no per-face clip-space transform in the shader. Ownership: the module
 * owns its cubemap/renderbuffer/FBO/shader and frees them in @ref
 * shadow_cube_destroy. Bake-free, per frame.
 */
#ifndef FERRUM_RENDERER_SHADOW_CUBE_H
#define FERRUM_RENDERER_SHADOW_CUBE_H

#include <stdint.h>

#include "ferrum/renderer/gl_loader.h"
#include "ferrum/renderer/render_scene.h"
#include "ferrum/renderer/shader_program.h"
#include "ferrum/renderer/shader_uniforms.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Point-light cube shadow map + its GL resources. */
typedef struct shadow_cube {
    shader_program_t       shader;   /**< distance-writing depth program. */
    shader_uniform_cache_t cache;
    uint32_t resolution;             /**< per-face pixel size. */
    float    near_plane;
    float    far_plane;
    uint32_t cube;                   /**< R32F cube-map ARRAY (6*max_lights faces). */
    uint32_t max_lights;             /**< cube-array capacity (shadow slots). */
    uint32_t depth_rb;               /**< shared depth renderbuffer. */
    uint32_t fbo;

    /* GL entry points (bound from the loader). */
    void (*glGenFramebuffers)(int32_t, uint32_t *);
    void (*glDeleteFramebuffers)(int32_t, const uint32_t *);
    void (*glBindFramebuffer)(uint32_t, uint32_t);
    void (*glFramebufferTexture2D)(uint32_t, uint32_t, uint32_t, uint32_t, int32_t);
    void (*glFramebufferTextureLayer)(uint32_t, uint32_t, uint32_t, int32_t, int32_t);
    void (*glFramebufferTexture)(uint32_t, uint32_t, uint32_t, int32_t);
    void (*glDrawElementsInstanced)(uint32_t, int32_t, uint32_t, const void *, int32_t);
    void (*glTexImage3D)(uint32_t, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, uint32_t, uint32_t, const void *);
    uint32_t depth_arr;              /**< layered depth 2D-array (6*max_lights). */
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
} shadow_cube_t;

/**
 * @brief Create the cubemap (R32F, @p resolution per face), a shared depth
 *        renderbuffer, an FBO and the distance shader. @p near_plane /
 *        @p far_plane bound the light's shadow range.
 */
bool shadow_cube_init(shadow_cube_t *sc, const gl_loader_t *loader,
                      uint32_t resolution, float near_plane, float far_plane,
                      uint32_t max_lights);

/**
 * @brief Clear EVERY slot's faces to "far" (call once per frame before the
 *        per-light renders; the layered FBO's clear touches all layers at once).
 */
void shadow_cube_clear(shadow_cube_t *sc);

/**
 * @brief Render the scene's depth-distance into cube-array SLOT @p slot's six
 *        faces from @p light_pos (so many point lights each get their own
 *        omnidirectional shadow). Caller re-binds its framebuffer/viewport after.
 * @param range Point-light cutoff distance: casters whose world AABB lies beyond
 *              @p range of @p light_pos are skipped (they cast no visible shadow).
 *              <= 0 disables the cull (draw every caster, as before) -- rpg-9u96.
 */
void shadow_cube_render_light(shadow_cube_t *sc, const render_scene_t *scene,
                              const float light_pos[3], uint32_t slot, float range);

/**
 * @brief Bind the cube-map ARRAY to @p unit and set u_shadow_cube_arr +
 *        u_shadow_far. Each shadowed light carries its slot in its packed data.
 */
void shadow_cube_bind(const shadow_cube_t *sc, shader_uniform_cache_t *cache,
                      const shader_program_t *program, uint32_t unit);

/** @brief Release all GL resources. NULL-safe. */
void shadow_cube_destroy(shadow_cube_t *sc);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_RENDERER_SHADOW_CUBE_H */
