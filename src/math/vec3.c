#include <math.h>

#include "ferrum/math/vec3.h"

vec3_t vec3_normalize_safe(vec3_t v, float epsilon) {
    float len_sq = vec3_dot(v, v);
    if (len_sq <= epsilon || len_sq == 0.0f) {
        vec3_t zero = {0.0f, 0.0f, 0.0f};
        return zero;
    }
    float inv = 1.0f / sqrtf(len_sq);
    return vec3_scale(v, inv);
}
