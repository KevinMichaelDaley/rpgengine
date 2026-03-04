#ifndef FERRUM_RENDERER_RENDER_PASS_TYPE_H
#define FERRUM_RENDERER_RENDER_PASS_TYPE_H

/**
 * @file render_pass_type.h
 * @brief Render pass type enum defining the 9-pass pipeline order.
 *
 * Passes execute in enum order (0 = first, 8 = last).  The enum
 * value doubles as the array index into the pipeline's pass array.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Render pass types in deterministic execution order.
 */
typedef enum render_pass_type {
    RENDER_PASS_SHADOW     = 0, /**< Per-light shadow map generation. */
    RENDER_PASS_DEPTH_PRE  = 1, /**< Optional depth pre-pass for early-Z. */
    RENDER_PASS_CASTER     = 2, /**< Precomputed shadow maps for static lights. */
    RENDER_PASS_LIGHT_CULL = 3, /**< Tiled light assignment. */
    RENDER_PASS_FORWARD    = 4, /**< Main shading: geometry + lighting. */
    RENDER_PASS_SKYBOX     = 5, /**< Drawn at max depth after forward. */
    RENDER_PASS_DEBUG      = 6, /**< Debug lines, gizmos, wireframes. */
    RENDER_PASS_POST       = 7, /**< Tone mapping, gamma, FXAA. */
    RENDER_PASS_UI         = 8, /**< 2D overlay. */
    RENDER_PASS_TYPE_COUNT = 9  /**< Sentinel — not a valid pass type. */
} render_pass_type_t;

/**
 * @brief Get a human-readable name for a pass type.
 *
 * @param type  Pass type (must be < RENDER_PASS_TYPE_COUNT).
 * @return Static string, or "unknown" for out-of-range values.
 */
static inline const char *render_pass_type_name(render_pass_type_t type) {
    static const char *NAMES[RENDER_PASS_TYPE_COUNT] = {
        "shadow", "depth_pre", "caster", "light_cull",
        "forward", "skybox", "debug", "post", "ui"
    };
    if ((int)type < 0 || (int)type >= RENDER_PASS_TYPE_COUNT) {
        return "unknown";
    }
    return NAMES[type];
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_RENDERER_RENDER_PASS_TYPE_H */
