#include <math.h>

#include "ferrum/math/vec3.h"

float vec3_dot(vec3_t a, vec3_t b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

vec3_t vec3_add(vec3_t a, vec3_t b) {
    vec3_t r = {a.x + b.x, a.y + b.y, a.z + b.z};
    return r;
}

vec3_t vec3_sub(vec3_t a, vec3_t b) {
    vec3_t r = {a.x - b.x, a.y - b.y, a.z - b.z};
    return r;
}

vec3_t vec3_scale(vec3_t v, float s) {
    vec3_t r = {v.x * s, v.y * s, v.z * s};
    return r;
}
