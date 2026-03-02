/**
 * @file quat_euler.c
 * @brief Quaternion from Euler angles (XYZ intrinsic order).
 *
 * Non-static functions: 1 (quat_from_euler).
 */

#include "ferrum/math/quat.h"
#include <math.h>

quat_t quat_from_euler(float x, float y, float z) {
    float cx = cosf(x * 0.5f), sx = sinf(x * 0.5f);
    float cy = cosf(y * 0.5f), sy = sinf(y * 0.5f);
    float cz = cosf(z * 0.5f), sz = sinf(z * 0.5f);

    return (quat_t){
        .x = sx * cy * cz + cx * sy * sz,
        .y = cx * sy * cz - sx * cy * sz,
        .z = cx * cy * sz + sx * sy * cz,
        .w = cx * cy * cz - sx * sy * sz,
    };
}
