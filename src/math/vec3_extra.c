#include <math.h>

#include "ferrum/math/vec3.h"

float vec3_magnitude(vec3_t v) {
    return sqrtf(vec3_dot(v, v));
}

vec3_t vec3_normalize_safe(vec3_t v, float epsilon) {
    float len_sq = vec3_dot(v, v);
    if (len_sq <= epsilon || len_sq == 0.0f) {
        vec3_t zero = {0.0f, 0.0f, 0.0f};
        return zero;
    }
    float inv = 1.0f / sqrtf(len_sq);
    return vec3_scale(v, inv);
}

vec3_t vec3_lerp(vec3_t a, vec3_t b, float t) {
    vec3_t r = {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
    return r;
}

vec3_t vec3_cross(vec3_t a, vec3_t b) {
    vec3_t r = {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    return r;
}
