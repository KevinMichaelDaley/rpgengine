#ifndef FERRUM_RENDERER_RENDER_CAMERA_H
#define FERRUM_RENDERER_RENDER_CAMERA_H

/** @file
 * @brief View camera for scene submission: view + projection matrices + eye.
 *
 * Held by the scene and consumed by every pass (forward+, shadows, deferred).
 * @ref render_camera_look_at builds all three from a look-at + perspective.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** A render camera: column-major view + projection and the world eye position. */
typedef struct render_camera {
    float view[16];  /**< world -> view (column-major). */
    float proj[16];  /**< view -> clip (column-major). */
    float eye[3];    /**< world-space eye position. */
} render_camera_t;

/**
 * @brief Build a camera from a look-at and a perspective projection.
 * @param cam         Output camera (non-NULL).
 * @param eye         World eye position (3 floats).
 * @param target      Look-at target (3 floats).
 * @param up          Up vector (3 floats).
 * @param fov_radians Vertical field of view in radians.
 * @param aspect      Width / height.
 * @param near_plane  Near clip distance (> 0).
 * @param far_plane   Far clip distance (> near).
 */
void render_camera_look_at(render_camera_t *cam, const float eye[3],
                           const float target[3], const float up[3],
                           float fov_radians, float aspect, float near_plane,
                           float far_plane);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_RENDERER_RENDER_CAMERA_H */
