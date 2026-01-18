#include <math.h>
#include <stddef.h>

#include "ferrum/math/quat.h"
#include "ferrum/math/mat4.h"

int quat_to_mat4(quat_t q, mat4_t *out) {
    if (out == NULL) {
        return -1;
    }
    q = quat_normalize_safe(q, 1e-6f);
    float xx = q.x * q.x;
    float yy = q.y * q.y;
    float zz = q.z * q.z;
    float xy = q.x * q.y;
    float xz = q.x * q.z;
    float yz = q.y * q.z;
    float wx = q.w * q.x;
    float wy = q.w * q.y;
    float wz = q.w * q.z;

    mat4_t m = {{
        1.0f - 2.0f * (yy + zz),
        2.0f * (xy + wz),
        2.0f * (xz - wy),
        0.0f,
        2.0f * (xy - wz),
        1.0f - 2.0f * (xx + zz),
        2.0f * (yz + wx),
        0.0f,
        2.0f * (xz + wy),
        2.0f * (yz - wx),
        1.0f - 2.0f * (xx + yy),
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        1.0f}};
    *out = m;
    return 0;
}
