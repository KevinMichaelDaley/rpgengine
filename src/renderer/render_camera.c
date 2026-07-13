#include "ferrum/renderer/render_camera.h"

#include <stddef.h>

#include "ferrum/math/mat4.h"
#include "ferrum/math/vec3.h"

void render_camera_look_at(render_camera_t *cam, const float eye[3],
                           const float target[3], const float up[3],
                           float fov_radians, float aspect, float near_plane,
                           float far_plane)
{
    if (cam == NULL) {
        return;
    }
    vec3_t e = { eye[0], eye[1], eye[2] };
    vec3_t t = { target[0], target[1], target[2] };
    vec3_t u = { up[0], up[1], up[2] };
    mat4_t view, proj;
    mat4_look_at(e, t, u, &view);
    mat4_perspective(fov_radians, aspect, near_plane, far_plane, &proj);
    for (int i = 0; i < 16; ++i) {
        cam->view[i] = view.m[i];
        cam->proj[i] = proj.m[i];
    }
    cam->eye[0] = eye[0];
    cam->eye[1] = eye[1];
    cam->eye[2] = eye[2];
}
