#ifndef FERRUM_DEMO_CAMERA_H
#define FERRUM_DEMO_CAMERA_H

/** @file
 * @brief FPS-style camera controller for the physics demo.
 */

#include <stdint.h>
#include "ferrum/math/vec3.h"
#include "ferrum/math/mat4.h"

#ifdef __cplusplus
extern "C" {
#endif

/** FPS camera state. */
typedef struct demo_camera {
    vec3_t position;
    float yaw;        /**< Radians, 0 = looking along +Z. */
    float pitch;      /**< Radians, clamped to [-89°, +89°]. */
    float move_speed;  /**< Units per second. */
    float sensitivity; /**< Radians per pixel of mouse delta. */
} demo_camera_t;

/**
 * @brief Initialize camera with defaults.
 *
 * Position at (0, 1.7, 0), looking along +Z (yaw=0, pitch=0).
 * Move speed: 5.0, sensitivity: 0.003.
 *
 * @param cam Camera to initialize (non-NULL).
 */
void demo_camera_init(demo_camera_t *cam);

/**
 * @brief Update camera from mouse delta and WASD flags.
 *
 * Yaw and pitch are adjusted by the mouse delta scaled by sensitivity.
 * Movement uses the flat (pitch-independent) forward/right vectors.
 *
 * @param cam        Camera to update (non-NULL).
 * @param mouse_dx   Horizontal mouse delta (pixels).
 * @param mouse_dy   Vertical mouse delta (pixels).
 * @param wasd_flags Bit 0=W, 1=A, 2=S, 3=D.
 * @param dt         Frame delta time (seconds).
 */
void demo_camera_update(demo_camera_t *cam, float mouse_dx, float mouse_dy,
                        uint8_t wasd_flags, float dt);

/**
 * @brief Build a view matrix from the current camera state.
 *
 * Uses mat4_look_at internally.
 *
 * @param cam Camera state (non-NULL).
 * @param out Output view matrix (non-NULL).
 */
void demo_camera_view_matrix(const demo_camera_t *cam, mat4_t *out);

/**
 * @brief Get the forward direction vector (unit length, in world space).
 *
 * Includes pitch so the vector points where the camera is looking.
 *
 * @param cam Camera state (non-NULL).
 * @return Forward direction unit vector.
 */
vec3_t demo_camera_forward(const demo_camera_t *cam);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_DEMO_CAMERA_H */
