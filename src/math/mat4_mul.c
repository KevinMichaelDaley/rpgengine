#include "ferrum/math/mat4.h"

mat4_t mat4_mul(mat4_t a, mat4_t b) {
    mat4_t r = {0};
    for (int c = 0; c < 4; ++c) {
        for (int r_idx = 0; r_idx < 4; ++r_idx) {
            r.m[c * 4 + r_idx] =
                a.m[0 * 4 + r_idx] * b.m[c * 4 + 0] +
                a.m[1 * 4 + r_idx] * b.m[c * 4 + 1] +
                a.m[2 * 4 + r_idx] * b.m[c * 4 + 2] +
                a.m[3 * 4 + r_idx] * b.m[c * 4 + 3];
        }
    }
    return r;
}

vec4_t mat4_mul_vec4(mat4_t m, vec4_t v) {
    vec4_t r = {
        m.m[0] * v.x + m.m[4] * v.y + m.m[8] * v.z + m.m[12] * v.w,
        m.m[1] * v.x + m.m[5] * v.y + m.m[9] * v.z + m.m[13] * v.w,
        m.m[2] * v.x + m.m[6] * v.y + m.m[10] * v.z + m.m[14] * v.w,
        m.m[3] * v.x + m.m[7] * v.y + m.m[11] * v.z + m.m[15] * v.w};
    return r;
}
