/**
 * @file viewport_camera_zoom.c
 * @brief Camera zoom, snap views, toggle projection, frame selection.
 *
 * Non-static functions: 4 (zoom, snap_view, toggle_projection,
 *                          frame_selection).
 */

#include "ferrum/editor/viewport/viewport_camera.h"
#include "ferrum/math/constants.h"
#include <math.h>

/** @brief Clamp a float to [lo, hi]. */
static float clampf_(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

void editor_camera_zoom(editor_camera_t *cam, float delta) {
    cam->distance += delta;
    cam->distance = clampf_(cam->distance,
                             EDITOR_CAMERA_MIN_DISTANCE,
                             EDITOR_CAMERA_MAX_DISTANCE);
}

void editor_camera_snap_view(editor_camera_t *cam, editor_snap_view_t view) {
    switch (view) {
    case EDITOR_VIEW_FRONT:
        cam->yaw = 0.0f;
        cam->pitch = 0.0f;
        break;
    case EDITOR_VIEW_BACK:
        cam->yaw = FERRUM_PI;
        cam->pitch = 0.0f;
        break;
    case EDITOR_VIEW_RIGHT:
        cam->yaw = FERRUM_PI_2;
        cam->pitch = 0.0f;
        break;
    case EDITOR_VIEW_LEFT:
        cam->yaw = -FERRUM_PI_2;
        cam->pitch = 0.0f;
        break;
    case EDITOR_VIEW_TOP:
        cam->yaw = 0.0f;
        cam->pitch = -FERRUM_PI_2;
        break;
    case EDITOR_VIEW_BOTTOM:
        cam->yaw = 0.0f;
        cam->pitch = FERRUM_PI_2;
        break;
    }
}

void editor_camera_toggle_projection(editor_camera_t *cam) {
    cam->orthographic = !cam->orthographic;
}

void editor_camera_frame_selection(editor_camera_t *cam,
                                    vec3_t aabb_min, vec3_t aabb_max) {
    /* Set focus to AABB center. */
    cam->focus.x = (aabb_min.x + aabb_max.x) * 0.5f;
    cam->focus.y = (aabb_min.y + aabb_max.y) * 0.5f;
    cam->focus.z = (aabb_min.z + aabb_max.z) * 0.5f;

    /* Compute AABB diagonal and set distance to fit. */
    float dx = aabb_max.x - aabb_min.x;
    float dy = aabb_max.y - aabb_min.y;
    float dz = aabb_max.z - aabb_min.z;
    float diagonal = sqrtf(dx * dx + dy * dy + dz * dz);

    /* Distance = half-diagonal / tan(fov/2), with a minimum. */
    float half_fov = cam->fov * 0.5f;
    float tan_fov = tanf(half_fov);
    float dist = (tan_fov > 0.001f) ? (diagonal * 0.5f / tan_fov) : diagonal;
    cam->distance = clampf_(dist, EDITOR_CAMERA_MIN_DISTANCE,
                             EDITOR_CAMERA_MAX_DISTANCE);
}
