#include <math.h>
#include <stddef.h>

#include "ferrum/math/mat4.h"

int mat4_perspective(float fov_radians, float aspect, float near_plane, float far_plane, mat4_t *out) {
    if (out == NULL || fov_radians <= 0.0f || aspect <= 0.0f || near_plane <= 0.0f || far_plane <= near_plane) {
        return -1;
    }
    float f = 1.0f / tanf(fov_radians * 0.5f);
    mat4_t m = {0};
    m.m[0] = f / aspect;
    m.m[5] = f;
    m.m[10] = (far_plane + near_plane) / (near_plane - far_plane);
    m.m[11] = -1.0f;
    m.m[14] = (2.0f * far_plane * near_plane) / (near_plane - far_plane);
    *out = m;
    return 0;
}

mat4_t mat4_ortho(float left, float right, float bottom, float top, float near_plane, float far_plane) {
    float rl = right - left;
    float tb = top - bottom;
    float fn = far_plane - near_plane;
    mat4_t m = {{
        2.0f / rl, 0.0f, 0.0f, 0.0f,
        0.0f, 2.0f / tb, 0.0f, 0.0f,
        0.0f, 0.0f, -2.0f / fn, 0.0f,
        -(right + left) / rl, -(top + bottom) / tb, -(far_plane + near_plane) / fn, 1.0f}};
    return m;
}
