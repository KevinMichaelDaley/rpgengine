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

mat4_t mat4_identity(void) {
    return mat4_make(1, 0, 0, 0,
                     0, 1, 0, 0,
                     0, 0, 1, 0,
                     0, 0, 0, 1);
}

mat4_t mat4_translation(float x, float y, float z) {
    mat4_t m = mat4_identity();
    m.m[12] = x;
    m.m[13] = y;
    m.m[14] = z;
    return m;
}

mat4_t mat4_scaling(float x, float y, float z) {
    return mat4_make(x, 0, 0, 0,
                     0, y, 0, 0,
                     0, 0, z, 0,
                     0, 0, 0, 1);
}
