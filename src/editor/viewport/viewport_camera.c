/**
 * @file viewport_camera.c
 * @brief Editor camera lifecycle and orbit/pan controls.
 *
 * Non-static functions: 4 (init, reset, orbit, pan).
 */

#include "ferrum/editor/viewport/viewport_camera.h"
#include "ferrum/math/constants.h"
#include <math.h>

/** @brief Maximum pitch in radians (89 degrees). */
static const float MAX_PITCH = 89.0f * FERRUM_PI / 180.0f;

/** @brief Clamp a float to [lo, hi]. */
static float clampf_(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

void editor_camera_init(editor_camera_t *cam) {
    cam->focus = (vec3_t){0.0f, 0.0f, 0.0f};
    cam->yaw = 0.0f;
    cam->pitch = 0.0f;
    cam->distance = 10.0f;
    cam->orthographic = false;
    cam->fov = 45.0f * FERRUM_PI / 180.0f;
    cam->near_plane = 0.1f;
    cam->far_plane = 5000.0f;
}

void editor_camera_reset(editor_camera_t *cam) {
    editor_camera_init(cam);
}

void editor_camera_orbit(editor_camera_t *cam, float delta_yaw,
                          float delta_pitch) {
    cam->yaw += delta_yaw;
    cam->pitch += delta_pitch;
    cam->pitch = clampf_(cam->pitch, -MAX_PITCH, MAX_PITCH);
}

void editor_camera_pan(editor_camera_t *cam, float dx, float dy) {
    /* Compute camera-local right and up vectors from yaw/pitch. */
    float cos_yaw = cosf(cam->yaw);
    float sin_yaw = sinf(cam->yaw);
    float cos_pitch = cosf(cam->pitch);
    float sin_pitch = sinf(cam->pitch);

    /* Right vector (always horizontal). */
    vec3_t right = {cos_yaw, 0.0f, -sin_yaw};

    /* Up vector (perpendicular to forward and right). */
    vec3_t up = {
        sin_yaw * sin_pitch,
        cos_pitch,
        cos_yaw * sin_pitch
    };

    /* Move focus by dx * right + dy * up. */
    cam->focus.x += dx * right.x + dy * up.x;
    cam->focus.y += dx * right.y + dy * up.y;
    cam->focus.z += dx * right.z + dy * up.z;
}
