/**
 * @file viewport_shading.h
 * @brief Per-viewport shading mode definitions.
 *
 * Shading mode controls how entities are rendered in the viewport:
 *   - Shaded: full render pipeline preview matching in-game look
 *   - Matcap: fixed material, half-lambert lighting, clay-like appearance
 *   - Unlit: flat solid color, no lighting
 *   - Wireframe: wireframe overlay rendering
 *
 * Ownership: pure value types, no resources.
 * Nullability: N/A (enum + inline helpers).
 * Error semantics: N/A.
 * Side effects: none.
 *
 * Public types: shading_mode_t (1 / 2-type rule).
 */
#ifndef FERRUM_EDITOR_VIEWPORT_SHADING_H
#define FERRUM_EDITOR_VIEWPORT_SHADING_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * @brief Shading mode for a viewport.
 */
typedef enum shading_mode {
    SHADING_MODE_SHADED    = 0, /**< Full render pipeline (in-game look). */
    SHADING_MODE_MATCAP    = 1, /**< Half-lambert clay-like fixed material. */
    SHADING_MODE_UNLIT     = 2, /**< Flat solid color, no lighting. */
    SHADING_MODE_WIREFRAME = 3, /**< Wireframe rendering. */
    SHADING_MODE_COUNT     = 4
} shading_mode_t;

/** @brief Get display name for a shading mode. */
static inline const char *shading_mode_name(shading_mode_t mode) {
    switch (mode) {
    case SHADING_MODE_SHADED:    return "Shaded";
    case SHADING_MODE_MATCAP:    return "Matcap";
    case SHADING_MODE_UNLIT:     return "Unlit";
    case SHADING_MODE_WIREFRAME: return "Wire";
    default:                     return "?";
    }
}

/** @brief Cycle to the next shading mode (wraps around). */
static inline shading_mode_t shading_mode_next(shading_mode_t mode) {
    return (shading_mode_t)(((uint8_t)mode + 1) % SHADING_MODE_COUNT);
}

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_VIEWPORT_SHADING_H */
