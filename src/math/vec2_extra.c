#include <math.h>

#include "ferrum/math/vec2.h"

float vec2_magnitude(vec2_t v) {
    return sqrtf(vec2_dot(v, v));
}

vec2_t vec2_normalize_safe(vec2_t v, float epsilon) {
    float len_sq = vec2_dot(v, v);
    if (len_sq <= epsilon || len_sq == 0.0f) {
        vec2_t zero = {0.0f, 0.0f};
        return zero;
    }
    float inv = 1.0f / sqrtf(len_sq);
    return vec2_scale(v, inv);
}

vec2_t vec2_lerp(vec2_t a, vec2_t b, float t) {
    vec2_t r = {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
    return r;
}
