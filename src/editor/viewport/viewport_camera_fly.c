/**
 * @file viewport_camera_fly.c
 * @brief Fly-mode camera movement: direct position control.
 *
 * In fly mode, distance is 0 and focus IS the eye position.
 * Movement is computed from yaw/pitch to get forward/right vectors.
 *
 * Non-static functions (3 / 4 limit):
 *   editor_camera_fly_move
 *   editor_camera_enter_fly
 *   editor_camera_exit_fly
 */

#include "ferrum/editor/viewport/viewport_camera.h"
#include <math.h>

void editor_camera_fly_move(editor_camera_t *cam, float forward,
                             float right, float up) {
    if (!cam) return;

    float cos_yaw   = cosf(cam->yaw);
    float sin_yaw   = sinf(cam->yaw);
    float cos_pitch = cosf(cam->pitch);
    float sin_pitch = sinf(cam->pitch);

    /* Forward direction from yaw + pitch. */
    float fwd_x = sin_yaw * cos_pitch;
    float fwd_y = sin_pitch;
    float fwd_z = cos_yaw * cos_pitch;

    /* Right direction (always horizontal, from yaw only). */
    float rgt_x =  cos_yaw;
    float rgt_z = -sin_yaw;

    cam->focus.x += fwd_x * forward + rgt_x * right;
    cam->focus.y += fwd_y * forward + up;
    cam->focus.z += fwd_z * forward + rgt_z * right;
}

void editor_camera_enter_fly(editor_camera_t *cam) {
    if (!cam) return;

    /* Move focus to current eye position, zero out distance. */
    vec3_t eye = editor_camera_eye_position(cam);
    cam->focus = eye;
    cam->distance = 0.0f;
}

void editor_camera_exit_fly(editor_camera_t *cam, float orbit_distance) {
    if (!cam) return;
    if (orbit_distance < EDITOR_CAMERA_MIN_DISTANCE) {
        orbit_distance = EDITOR_CAMERA_MIN_DISTANCE;
    }

    /* Place focus ahead of current position along look direction. */
    float cos_yaw   = cosf(cam->yaw);
    float sin_yaw   = sinf(cam->yaw);
    float cos_pitch = cosf(cam->pitch);
    float sin_pitch = sinf(cam->pitch);

    vec3_t pos = cam->focus; /* Current eye position in fly mode. */
    cam->focus.x = pos.x + sin_yaw * cos_pitch * orbit_distance;
    cam->focus.y = pos.y + sin_pitch * orbit_distance;
    cam->focus.z = pos.z + cos_yaw * cos_pitch * orbit_distance;
    cam->distance = orbit_distance;
}
