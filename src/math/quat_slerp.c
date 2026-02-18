#include <math.h>

#include "ferrum/math/common.h"
#include "ferrum/math/quat.h"

quat_t quat_slerp(quat_t a, quat_t b, float t, float epsilon) {
    t = fr_clampf(t, 0.0f, 1.0f);
    float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (dot < 0.0f) {
        dot = -dot;
        b.x = -b.x;
        b.y = -b.y;
        b.z = -b.z;
        b.w = -b.w;
    }
    const float DOT_THRESHOLD = 0.9995f;
    if (dot > DOT_THRESHOLD) {
        quat_t result = {
            a.x + t * (b.x - a.x),
            a.y + t * (b.y - a.y),
            a.z + t * (b.z - a.z),
            a.w + t * (b.w - a.w)};
        return quat_normalize_safe(result, epsilon);
    }

    float theta_0 = acosf(dot);
    float theta = theta_0 * t;
    float sin_theta = sinf(theta);
    float sin_theta_0 = sinf(theta_0);

    float s0 = cosf(theta) - dot * sin_theta / sin_theta_0;
    float s1 = sin_theta / sin_theta_0;

    quat_t out = {a.x * s0 + b.x * s1,
                  a.y * s0 + b.y * s1,
                  a.z * s0 + b.z * s1,
                  a.w * s0 + b.w * s1};
    return quat_normalize_safe(out, epsilon);
}
