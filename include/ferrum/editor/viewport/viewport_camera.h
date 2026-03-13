/**
 * @file viewport_camera.h
 * @brief Editor viewport camera — orbit, pan, zoom, snap views.
 *
 * An orbit camera that revolves around a focus point. Supports perspective
 * and orthographic projection, snap views, and screen-to-ray casting
 * for entity picking.
 *
 * Ownership: caller owns the editor_camera_t; no dynamic allocation.
 * Nullability: all pointer params must be non-NULL.
 * Error semantics: matrix functions return 0 on success, -1 on error.
 * Side effects: none (pure state + math).
 *
 * Public types: editor_camera_t, editor_ray_t (2-type rule).
 */
#ifndef FERRUM_EDITOR_VIEWPORT_CAMERA_H
#define FERRUM_EDITOR_VIEWPORT_CAMERA_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "ferrum/math/vec2.h"
#include "ferrum/math/vec3.h"
#include "ferrum/math/mat4.h"

/* ---- Constants ---- */

/** @brief Minimum orbit distance (prevents clipping into focus). */
#define EDITOR_CAMERA_MIN_DISTANCE 0.1f
/** @brief Maximum orbit distance. */
#define EDITOR_CAMERA_MAX_DISTANCE 10000.0f

/** @brief Snap view identifiers (number row keys). */
typedef enum editor_snap_view {
    EDITOR_VIEW_FRONT  = 1,  /**< 1 key: looking along +Z. */
    EDITOR_VIEW_BACK   = 11, /**< Ctrl+1: looking along -Z. */
    EDITOR_VIEW_RIGHT  = 3,  /**< 3 key: looking along -X. */
    EDITOR_VIEW_LEFT   = 13, /**< Ctrl+3: looking along +X. */
    EDITOR_VIEW_TOP    = 7,  /**< 7 key: looking down -Y. */
    EDITOR_VIEW_BOTTOM = 17  /**< Ctrl+7: looking up +Y. */
} editor_snap_view_t;

/* ---- Types ---- */

/**
 * @brief Editor orbit camera state.
 *
 * The camera orbits around a focus point at a given distance.
 * Yaw rotates around the world Y axis; pitch tilts up/down.
 */
typedef struct editor_camera {
    vec3_t focus;          /**< World-space focus (orbit center). */
    float  yaw;            /**< Horizontal angle in radians (around Y). */
    float  pitch;          /**< Vertical angle in radians (clamped ±89°). */
    float  distance;       /**< Radial distance from focus to eye. */
    bool   orthographic;   /**< True for orthographic projection. */
    float  fov;            /**< Vertical field of view in radians. */
    float  near_plane;     /**< Near clip plane distance. */
    float  far_plane;      /**< Far clip plane distance. */
} editor_camera_t;

/**
 * @brief A ray in world space (origin + normalized direction).
 */
typedef struct editor_ray {
    vec3_t origin;     /**< Ray start point. */
    vec3_t direction;  /**< Normalized direction. */
} editor_ray_t;

/* ---- Lifecycle ---- */

/**
 * @brief Initialize camera with sensible defaults.
 *
 * Focus at origin, yaw=0, pitch=0, distance=10, perspective,
 * fov=45°, near=0.1, far=5000.
 *
 * @param cam  Camera to initialize (non-NULL).
 */
void editor_camera_init(editor_camera_t *cam);

/**
 * @brief Reset camera to default state (same as init).
 * @param cam  Camera to reset (non-NULL).
 */
void editor_camera_reset(editor_camera_t *cam);

/* ---- Controls ---- */

/**
 * @brief Orbit the camera by delta yaw and pitch (in radians).
 *
 * Pitch is clamped to ±89 degrees to prevent gimbal flip.
 *
 * @param cam         Camera (non-NULL).
 * @param delta_yaw   Horizontal rotation delta (radians).
 * @param delta_pitch Vertical rotation delta (radians).
 */
void editor_camera_orbit(editor_camera_t *cam, float delta_yaw,
                          float delta_pitch);

/**
 * @brief Pan the camera in screen space.
 *
 * Moves the focus point by dx along camera-right and dy along camera-up.
 *
 * @param cam  Camera (non-NULL).
 * @param dx   Screen-space horizontal delta.
 * @param dy   Screen-space vertical delta.
 */
void editor_camera_pan(editor_camera_t *cam, float dx, float dy);

/* ---- viewport_camera_zoom.c ---- */

/**
 * @brief Zoom (dolly) the camera by adjusting distance.
 *
 * Positive delta moves away, negative moves closer.
 * Distance is clamped to [MIN_DISTANCE, MAX_DISTANCE].
 *
 * @param cam    Camera (non-NULL).
 * @param delta  Distance delta.
 */
void editor_camera_zoom(editor_camera_t *cam, float delta);

/**
 * @brief Snap camera to a predefined view (number row shortcuts).
 * @param cam   Camera (non-NULL).
 * @param view  Snap view identifier.
 */
void editor_camera_snap_view(editor_camera_t *cam, editor_snap_view_t view);

/**
 * @brief Toggle between perspective and orthographic projection.
 * @param cam  Camera (non-NULL).
 */
void editor_camera_toggle_projection(editor_camera_t *cam);

/**
 * @brief Frame the camera to fit an axis-aligned bounding box.
 *
 * Sets focus to AABB center and adjusts distance to fit the box.
 *
 * @param cam       Camera (non-NULL).
 * @param aabb_min  Minimum corner of AABB.
 * @param aabb_max  Maximum corner of AABB.
 */
void editor_camera_frame_selection(editor_camera_t *cam,
                                    vec3_t aabb_min, vec3_t aabb_max);

/* ---- Queries ---- */

/**
 * @brief Compute the eye (camera) position in world space.
 * @param cam  Camera (non-NULL).
 * @return Eye position.
 */
vec3_t editor_camera_eye_position(const editor_camera_t *cam);

/**
 * @brief Compute the view matrix.
 * @param cam  Camera (non-NULL).
 * @param out  Output view matrix (non-NULL).
 * @return 0 on success, -1 on error.
 */
int editor_camera_view_matrix(const editor_camera_t *cam, mat4_t *out);

/**
 * @brief Compute the projection matrix.
 * @param cam     Camera (non-NULL).
 * @param aspect  Viewport aspect ratio (width/height).
 * @param out     Output projection matrix (non-NULL).
 * @return 0 on success, -1 on error.
 */
int editor_camera_projection_matrix(const editor_camera_t *cam,
                                     float aspect, mat4_t *out);

/**
 * @brief Cast a ray from screen coordinates into world space.
 *
 * Screen position is normalized [0,1]: (0,0)=top-left, (1,1)=bottom-right.
 *
 * @param cam            Camera (non-NULL).
 * @param screen_pos     Normalized screen coordinates.
 * @param viewport_size  Viewport size in pixels (for aspect ratio).
 * @param out            Output ray (non-NULL).
 * @return 0 on success, -1 on error.
 */
int editor_camera_screen_to_ray(const editor_camera_t *cam,
                                 vec2_t screen_pos, vec2_t viewport_size,
                                 editor_ray_t *out);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_VIEWPORT_CAMERA_H */
