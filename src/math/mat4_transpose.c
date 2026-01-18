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

mat4_t mat4_transpose(mat4_t m) {
    return mat4_make(m.m[0], m.m[4], m.m[8], m.m[12],
                     m.m[1], m.m[5], m.m[9], m.m[13],
                     m.m[2], m.m[6], m.m[10], m.m[14],
                     m.m[3], m.m[7], m.m[11], m.m[15]);
}
