#include <math.h>
#include <stddef.h>

#include "ferrum/math/mat4.h"

static int vec3_is_zero(vec3_t v, float epsilon) {
    return fabsf(v.x) <= epsilon && fabsf(v.y) <= epsilon && fabsf(v.z) <= epsilon;
}

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

int mat4_look_at(vec3_t eye, vec3_t target, vec3_t up, mat4_t *out) {
    if (out == NULL) {
        return -1;
    }
    vec3_t f = vec3_sub(target, eye);
    if (vec3_is_zero(f, 1e-6f)) {
        return -1;
    }
    f = vec3_normalize_safe(f, 1e-6f);
    vec3_t s = vec3_cross(f, up);
    if (vec3_is_zero(s, 1e-6f)) {
        return -1;
    }
    s = vec3_normalize_safe(s, 1e-6f);
    vec3_t u = vec3_cross(s, f);

    mat4_t m = mat4_make(s.x, u.x, -f.x, 0.0f,
                         s.y, u.y, -f.y, 0.0f,
                         s.z, u.z, -f.z, 0.0f,
                         -vec3_dot(s, eye), -vec3_dot(u, eye), vec3_dot(f, eye), 1.0f);
    *out = m;
    return 0;
}
