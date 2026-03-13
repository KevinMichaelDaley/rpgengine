/**
 * @file viewport_camera_query.c
 * @brief Camera query functions: eye position, view/projection matrices,
 *        screen-to-ray.
 *
 * Non-static functions: 4 (eye_position, view_matrix, projection_matrix,
 *                          screen_to_ray).
 */

#include "ferrum/editor/viewport/viewport_camera.h"
#include "ferrum/math/constants.h"
#include <math.h>

vec3_t editor_camera_eye_position(const editor_camera_t *cam) {
    /* Eye = focus + spherical-to-cartesian offset.
     * Yaw rotates around Y, pitch tilts up/down.
     * At yaw=0, pitch=0, camera is at focus + (0, 0, distance). */
    float cos_pitch = cosf(cam->pitch);
    float sin_pitch = sinf(cam->pitch);
    float cos_yaw = cosf(cam->yaw);
    float sin_yaw = sinf(cam->yaw);

    vec3_t eye;
    eye.x = cam->focus.x + cam->distance * sin_yaw * cos_pitch;
    eye.y = cam->focus.y + cam->distance * sin_pitch;
    eye.z = cam->focus.z + cam->distance * cos_yaw * cos_pitch;

    return eye;
}

int editor_camera_view_matrix(const editor_camera_t *cam, mat4_t *out) {
    if (!cam || !out) return -1;

    vec3_t eye = editor_camera_eye_position(cam);
    vec3_t up = {0.0f, 1.0f, 0.0f};

    return mat4_look_at(eye, cam->focus, up, out);
}

int editor_camera_projection_matrix(const editor_camera_t *cam,
                                     float aspect, mat4_t *out) {
    if (!cam || !out || aspect <= 0.0f) return -1;

    if (cam->orthographic) {
        /* Orthographic: use distance to scale the view volume. */
        float half_h = cam->distance * 0.5f;
        float half_w = half_h * aspect;
        *out = mat4_ortho(-half_w, half_w, -half_h, half_h,
                           cam->near_plane, cam->far_plane);
        return 0;
    } else {
        return mat4_perspective(cam->fov, aspect,
                                 cam->near_plane, cam->far_plane, out);
    }
}

int editor_camera_screen_to_ray(const editor_camera_t *cam,
                                 vec2_t screen_pos, vec2_t viewport_size,
                                 editor_ray_t *out) {
    if (!cam || !out) return -1;
    if (viewport_size.x <= 0.0f || viewport_size.y <= 0.0f) return -1;

    float aspect = viewport_size.x / viewport_size.y;

    /* Compute view and projection matrices. */
    mat4_t view, proj;
    if (editor_camera_view_matrix(cam, &view) != 0) return -1;
    if (editor_camera_projection_matrix(cam, aspect, &proj) != 0) return -1;

    /* Convert screen [0,1] to NDC [-1,1]. Y is flipped (0=top). */
    float ndc_x = screen_pos.x * 2.0f - 1.0f;
    float ndc_y = 1.0f - screen_pos.y * 2.0f;

    /* Inverse projection: unproject NDC point on near plane. */
    mat4_t inv_proj;
    if (mat4_inverse(proj, &inv_proj) != 0) return -1;

    mat4_t inv_view;
    if (mat4_inverse(view, &inv_view) != 0) return -1;

    /* Point on near plane in clip space. */
    vec4_t clip_near = {ndc_x, ndc_y, -1.0f, 1.0f};
    vec4_t eye_near = mat4_mul_vec4(inv_proj, clip_near);
    /* Perspective divide. */
    if (fabsf(eye_near.w) > 1e-8f) {
        eye_near.x /= eye_near.w;
        eye_near.y /= eye_near.w;
        eye_near.z /= eye_near.w;
    }
    eye_near.w = 1.0f;

    /* Transform to world space. */
    vec4_t world_near = mat4_mul_vec4(inv_view, eye_near);

    /* Point on far plane. */
    vec4_t clip_far = {ndc_x, ndc_y, 1.0f, 1.0f};
    vec4_t eye_far = mat4_mul_vec4(inv_proj, clip_far);
    if (fabsf(eye_far.w) > 1e-8f) {
        eye_far.x /= eye_far.w;
        eye_far.y /= eye_far.w;
        eye_far.z /= eye_far.w;
    }
    eye_far.w = 1.0f;
    vec4_t world_far = mat4_mul_vec4(inv_view, eye_far);

    /* Ray from near to far. */
    vec3_t origin = {world_near.x, world_near.y, world_near.z};
    vec3_t far_pt = {world_far.x, world_far.y, world_far.z};
    vec3_t dir = vec3_sub(far_pt, origin);
    dir = vec3_normalize_safe(dir, 1e-8f);

    out->origin = editor_camera_eye_position(cam);
    out->direction = dir;

    return 0;
}
