#include <math.h>

#include "ferrum/math/vec4.h"

float vec4_dot(vec4_t a, vec4_t b) {
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

vec4_t vec4_add(vec4_t a, vec4_t b) {
    vec4_t r = {a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w};
    return r;
}

vec4_t vec4_sub(vec4_t a, vec4_t b) {
    vec4_t r = {a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w};
    return r;
}

vec4_t vec4_scale(vec4_t v, float s) {
    vec4_t r = {v.x * s, v.y * s, v.z * s, v.w * s};
    return r;
}
