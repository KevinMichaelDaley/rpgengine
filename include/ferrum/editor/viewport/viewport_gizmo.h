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
#include <stdint.h>
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
 *
 * Single-axis values (X, Y, Z) constrain to one axis.
 * Planar values (XY, XZ, YZ) constrain to a plane defined by two axes.
 */
typedef enum gizmo_axis {
    GIZMO_AXIS_NONE = 0,
    GIZMO_AXIS_X    = 1,
    GIZMO_AXIS_Y    = 2,
    GIZMO_AXIS_Z    = 3,
    GIZMO_AXIS_XY   = 4,  /**< XY plane (free X + Y, locked Z). */
    GIZMO_AXIS_XZ   = 5,  /**< XZ plane (free X + Z, locked Y). */
    GIZMO_AXIS_YZ   = 6   /**< YZ plane (free Y + Z, locked X). */
} gizmo_axis_t;

/** @brief True if the axis is a planar constraint (XY, XZ, or YZ). */
static inline bool gizmo_axis_is_planar(gizmo_axis_t axis) {
    return axis >= GIZMO_AXIS_XY && axis <= GIZMO_AXIS_YZ;
}

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

    /** Rotation arc quadrant signs (per ring, +1 or -1).
     *  Determines which 90-degree quadrant each arc renders in.
     *  Updated with hysteresis by gizmo_update_arc_quadrants(). */
    int8_t            arc_sign_u[3]; /**< Sign for each ring's u tangent. */
    int8_t            arc_sign_v[3]; /**< Sign for each ring's v tangent. */
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

/* ---- Arc quadrant selection ---- */

/**
 * @brief Update rotation arc quadrant selection based on camera position.
 *
 * Picks the quadrant for each rotation arc that best faces the camera,
 * with hysteresis to prevent flickering at decision boundaries.
 *
 * @param gizmo    Gizmo state (non-NULL, arc_sign fields updated).
 * @param eye_pos  Camera eye position in world space.
 */
void gizmo_update_arc_quadrants(gizmo_state_t *gizmo, vec3_t eye_pos);

/* ---- Hit testing ---- */

/**
 * @brief Test which gizmo axis a ray hits.
 *
 * For rotate mode, projects ring sample points to screen space and
 * picks the ring closest to the cursor in 2D (pixel distance).
 * For translate/scale, uses 3D ray-segment distance.
 *
 * @param gizmo       Gizmo state (non-NULL).
 * @param ray         World-space ray (non-NULL).
 * @param gizmo_scale Visual scale of gizmo (screen-size compensation).
 * @param vp          View-projection matrix (for screen-space ring test).
 * @param screen_x    Cursor X in normalized viewport coords [0,1].
 * @param screen_y    Cursor Y in normalized viewport coords [0,1].
 * @return Hit axis, or GIZMO_AXIS_NONE if no hit.
 */
gizmo_axis_t gizmo_hit_test(const gizmo_state_t *gizmo,
                              const struct editor_ray *ray, float gizmo_scale,
                              const mat4_t *vp, float screen_x, float screen_y);

/**
 * @brief Compute per-ring screen-space distances for rotation hit testing.
 *
 * For each of the 3 rotation arcs (X, Y, Z rings), computes the 2D
 * screen-space distance from the cursor to the arc outline. Used by
 * per-object gizmo mode to compare ring distances across multiple
 * gizmos before applying the hit threshold.
 *
 * @param gizmo       Gizmo state (non-NULL).
 * @param gizmo_scale Visual scale (world units).
 * @param vp          View-projection matrix (non-NULL).
 * @param screen_x    Cursor X in normalized viewport coords [0,1].
 * @param screen_y    Cursor Y in normalized viewport coords [0,1].
 * @param out_dists   Output array of 3 distances [X, Y, Z rings].
 *                    1e30 if projection failed.
 */
void gizmo_ring_screen_distances(const gizmo_state_t *gizmo,
                                   float gizmo_scale,
                                   const mat4_t *vp,
                                   float screen_x, float screen_y,
                                   float out_dists[3]);

/**
 * @brief Screen-space hit threshold for rotation rings.
 *
 * Exposed so per-object pick can apply the same threshold used
 * by gizmo_hit_test() internally.
 */
#define GIZMO_RING_HIT_THRESHOLD 0.08f

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
