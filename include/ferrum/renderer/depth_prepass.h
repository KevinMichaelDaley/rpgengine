#ifndef FERRUM_RENDERER_DEPTH_PREPASS_H
#define FERRUM_RENDERER_DEPTH_PREPASS_H

#include "ferrum/renderer/gl_loader.h"
#include "ferrum/renderer/render_scene.h"
#include "ferrum/renderer/shader_program.h"
#include "ferrum/renderer/shader_uniforms.h"

/** @file
 * @brief Z-only pre-pass: fills the depth buffer from a scene's opaque meshes.
 *
 * Runs before clustered light culling (which reads depth) and the forward+
 * shading pass (which benefits from early-Z). Draws every renderable with a
 * position-only shader, colour writes masked off and depth writes on, into the
 * currently-bound framebuffer's depth attachment (the caller owns the target
 * and clears it).
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Depth pre-pass state: the depth-only program + the GL state entry points. */
typedef struct depth_prepass {
    shader_program_t       shader;
    shader_uniform_cache_t cache;
    void (*glColorMask)(uint8_t r, uint8_t g, uint8_t b, uint8_t a);
    void (*glDepthMask)(uint8_t flag);
    void (*glEnable)(uint32_t cap);
    void (*glDepthFunc)(uint32_t func);
} depth_prepass_t;

/**
 * @brief Create the depth pre-pass (compiles the position-only program and
 *        binds the GL state entry points from @p loader).
 */
shader_program_status_t depth_prepass_init(depth_prepass_t *pass,
                                           const gl_loader_t *loader);

/**
 * @brief Draw every VISIBLE renderable in @p scene into the bound depth buffer
 *        (colour writes masked off, depth writes + LESS test on; colour mask
 *        restored afterwards). The caller clears/binds the depth target.
 *        Renderables outside the camera frustum -- or, when @p draw_distance > 0,
 *        beyond that far cutoff -- are skipped so the pre-pass draws the same set
 *        as the forward pass (rpg-0rs4).
 * @param draw_distance Far cull distance in world units; 0 = unlimited.
 */
void depth_prepass_execute(depth_prepass_t *pass, const render_scene_t *scene,
                           float draw_distance);

/**
 * @brief Destroy the depth pre-pass program. NULL-safe.
 */
void depth_prepass_destroy(depth_prepass_t *pass);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_RENDERER_DEPTH_PREPASS_H */
