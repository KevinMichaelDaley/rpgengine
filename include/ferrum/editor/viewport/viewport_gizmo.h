/**
 * @file viewport_gizmo.h
 * @brief Transform gizmo state, hit testing, and drag computation.
 *
 * Provides move/rotate/scale gizmos with per-axis hit testing and
 * constrained drag delta computation.
 *
 * Ownership: caller owns gizmo_state_t; no dynamic allocation.
 * Nullability: all pointer params must be non-NULL.
 * Error semantics: hit_test returns GIZMO_AXIS_NONE on miss.
 * Side effects: none (pure state + math).
 *
 * Public types: gizmo_state_t (struct), gizmo_axis_t (enum).
 * Note: gizmo_mode_t is an additional enum but kept minimal.
 */
#ifndef FERRUM_EDITOR_VIEWPORT_GIZMO_H
#define FERRUM_EDITOR_VIEWPORT_GIZMO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "ferrum/math/vec3.h"
#include "ferrum/math/mat4.h"
#include "ferrum/editor/viewport/transform_basis.h"

/* Forward declarations. */
struct editor_ray;

/**
 * @brief Gizmo transform mode.
 */
typedef enum gizmo_mode {
    GIZMO_MODE_NONE      = -1,  /**< No gizmo (selection-only mode). */
    GIZMO_MODE_TRANSLATE = 0,
    GIZMO_MODE_ROTATE    = 1,
    GIZMO_MODE_SCALE     = 2
} gizmo_mode_t;

/**
 * @brief Gizmo axis identifier.
 */
typedef enum gizmo_axis {
    GIZMO_AXIS_NONE = 0,
    GIZMO_AXIS_X    = 1,
    GIZMO_AXIS_Y    = 2,
    GIZMO_AXIS_Z    = 3
} gizmo_axis_t;

/**
 * @brief Gizmo state — current mode, position, active axis, drag state.
 */
typedef struct gizmo_state {
    gizmo_mode_t      mode;          /**< Current transform mode. */
    gizmo_axis_t      active_axis;   /**< Currently hovered/active axis. */
    transform_basis_t basis;         /**< Coordinate space for transforms. */
    vec3_t            position;      /**< Gizmo world position (from selection). */
    mat4_t            orientation;   /**< Basis orientation matrix (rotation only). */
    bool              dragging;      /**< True if user is dragging a gizmo axis. */
} gizmo_state_t;

/* ---- Lifecycle ---- */

/**
 * @brief Initialize gizmo to default state.
 * @param gizmo  Gizmo to initialize (non-NULL).
 */
void gizmo_state_init(gizmo_state_t *gizmo);

/**
 * @brief Set the gizmo transform mode.
 * @param gizmo  Gizmo (non-NULL).
 * @param mode   New mode.
 */
void gizmo_state_set_mode(gizmo_state_t *gizmo, gizmo_mode_t mode);

/* ---- Hit testing ---- */

/**
 * @brief Test which gizmo axis a ray hits.
 *
 * Tests the ray against axis-aligned cylinders (translate), tori (rotate),
 * or cubes (scale) at the gizmo position.
 *
 * @param gizmo       Gizmo state (non-NULL).
 * @param ray         World-space ray (non-NULL).
 * @param gizmo_scale Visual scale of gizmo (screen-size compensation).
 * @return Hit axis, or GIZMO_AXIS_NONE if no hit.
 */
gizmo_axis_t gizmo_hit_test(const gizmo_state_t *gizmo,
                              const struct editor_ray *ray, float gizmo_scale);

/**
 * @brief Compute constrained drag delta between two world positions.
 *
 * Projects the drag movement onto the active axis.
 *
 * @param gizmo         Gizmo state (non-NULL, should have active_axis set).
 * @param drag_start    World position where drag started.
 * @param drag_current  Current world position.
 * @return Constrained delta vector (zero if no axis active).
 */
vec3_t gizmo_compute_drag_delta(const gizmo_state_t *gizmo,
                                  vec3_t drag_start, vec3_t drag_current);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_VIEWPORT_GIZMO_H */
