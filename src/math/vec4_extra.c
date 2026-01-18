#include <math.h>

#include "ferrum/math/vec4.h"

float vec4_magnitude(vec4_t v) {
    return sqrtf(vec4_dot(v, v));
}

vec4_t vec4_normalize_safe(vec4_t v, float epsilon) {
    float len_sq = vec4_dot(v, v);
    if (len_sq <= epsilon || len_sq == 0.0f) {
        vec4_t zero = {0.0f, 0.0f, 0.0f, 0.0f};
        return zero;
    }
    float inv = 1.0f / sqrtf(len_sq);
    return vec4_scale(v, inv);
}

vec4_t vec4_lerp(vec4_t a, vec4_t b, float t) {
    vec4_t r = {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t,
                a.w + (b.w - a.w) * t};
    return r;
}
