#include <math.h>

#include "ferrum/math/vec2.h"

float vec2_dot(vec2_t a, vec2_t b) {
    return a.x * b.x + a.y * b.y;
}

vec2_t vec2_add(vec2_t a, vec2_t b) {
    vec2_t r = {a.x + b.x, a.y + b.y};
    return r;
}

vec2_t vec2_sub(vec2_t a, vec2_t b) {
    vec2_t r = {a.x - b.x, a.y - b.y};
    return r;
}

vec2_t vec2_scale(vec2_t v, float s) {
    vec2_t r = {v.x * s, v.y * s};
    return r;
}
