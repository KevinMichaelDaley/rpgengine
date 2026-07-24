/**
 * @file refl_octa.c
 * @brief Octahedral encode/decode (see refl_octa.h).
 */
#include "ferrum/renderer/gi/refl_octa.h"

#include <math.h>
#include <stddef.h>

/* Fold helper: sign(v) with sign(0) = 1 (keeps the seam deterministic). */
static float oct_sign(float v) { return (v >= 0.0f) ? 1.0f : -1.0f; }

void refl_octa_encode(const float dir[3], float uv[2])
{
    if (dir == NULL || uv == NULL)
        return;
    float x = dir[0], y = dir[1], z = dir[2];
    float l1 = fabsf(x) + fabsf(y) + fabsf(z);
    if (l1 < 1e-8f) {              /* degenerate: park at the +z centre. */
        uv[0] = 0.5f;
        uv[1] = 0.5f;
        return;
    }
    x /= l1;
    y /= l1;
    z /= l1;
    if (z < 0.0f) {                /* fold the lower hemisphere outward. */
        float ox = (1.0f - fabsf(y)) * oct_sign(x);
        float oy = (1.0f - fabsf(x)) * oct_sign(y);
        x = ox;
        y = oy;
    }
    uv[0] = x * 0.5f + 0.5f;
    uv[1] = y * 0.5f + 0.5f;
}

void refl_octa_decode(const float uv[2], float dir[3])
{
    if (uv == NULL || dir == NULL)
        return;
    float u = uv[0], v = uv[1];
    if (u < 0.0f) u = 0.0f;
    if (u > 1.0f) u = 1.0f;
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    float x = u * 2.0f - 1.0f;
    float y = v * 2.0f - 1.0f;
    float z = 1.0f - fabsf(x) - fabsf(y);
    if (z < 0.0f) {                /* unfold the outer triangles. */
        float ox = (1.0f - fabsf(y)) * oct_sign(x);
        float oy = (1.0f - fabsf(x)) * oct_sign(y);
        x = ox;
        y = oy;
    }
    float len = sqrtf(x * x + y * y + z * z);
    if (len < 1e-8f) {
        dir[0] = 0.0f;
        dir[1] = 0.0f;
        dir[2] = 1.0f;
        return;
    }
    dir[0] = x / len;
    dir[1] = y / len;
    dir[2] = z / len;
}
