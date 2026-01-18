#include <math.h>

#include "ferrum/math/mat4.h"

static mat4_t mat4_make(float m00, float m01, float m02, float m03,
                        float m10, float m11, float m12, float m13,
                        float m20, float m21, float m22, float m23,
                        float m30, float m31, float m32, float m33) {
    mat4_t m = {{m00, m01, m02, m03,
                 m10, m11, m12, m13,
                 m20, m21, m22, m23,
                 m30, m31, m32, m33}};
    return m;
}

mat4_t mat4_rotation_x(float radians) {
    float c = cosf(radians);
    float s = sinf(radians);
    return mat4_make(1, 0, 0, 0,
                     0, c, s, 0,
                     0, -s, c, 0,
                     0, 0, 0, 1);
}

mat4_t mat4_rotation_y(float radians) {
    float c = cosf(radians);
    float s = sinf(radians);
    return mat4_make(c, 0, -s, 0,
                     0, 1, 0, 0,
                     s, 0, c, 0,
                     0, 0, 0, 1);
}

mat4_t mat4_rotation_z(float radians) {
    float c = cosf(radians);
    float s = sinf(radians);
    return mat4_make(c, s, 0, 0,
                     -s, c, 0, 0,
                     0, 0, 1, 0,
                     0, 0, 0, 1);
}
