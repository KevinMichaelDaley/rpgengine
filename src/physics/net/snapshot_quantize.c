/**
 * @file snapshot_quantize.c
 * @brief Quantization/dequantization utilities for network snapshots.
 *
 * Provides vec3 and quaternion quantization for compact wire format.
 * Position/velocity use a configurable scale (typically 1000 for mm).
 * Quaternions use smallest-3 encoding with snorm16 components.
 */

#include "ferrum/physics/snapshot.h"

#include "ferrum/math/common.h"

#include <math.h>

/* ── Internal constants ─────────────────────────────────────────── */

/** Maximum value for snorm16 mapping [-1, 1] → [-32767, 32767]. */
#define SNORM16_MAX 32767.0f

/* ── Public API ─────────────────────────────────────────────────── */

void phys_quantize_vec3(phys_vec3_t v, int16_t out[3], float scale)
{
    float components[3] = {v.x, v.y, v.z};
    for (int i = 0; i < 3; i++) {
        float scaled = components[i] * scale;
        scaled = fr_clampf(scaled, -32767.0f, 32767.0f);
        out[i] = (int16_t)roundf(scaled);
    }
}

phys_vec3_t phys_dequantize_vec3(const int16_t in[3], float inv_scale)
{
    phys_vec3_t result;
    result.x = (float)in[0] * inv_scale;
    result.y = (float)in[1] * inv_scale;
    result.z = (float)in[2] * inv_scale;
    return result;
}

void phys_quantize_quat(phys_quat_t q, int16_t out[3])
{
    /* Access quaternion components as an array for indexed lookup. */
    float components[4] = {q.x, q.y, q.z, q.w};

    /* Find index of the component with the largest absolute value. */
    int max_idx = 0;
    float max_abs = fabsf(components[0]);
    for (int i = 1; i < 4; i++) {
        float a = fabsf(components[i]);
        if (a > max_abs) {
            max_abs = a;
            max_idx = i;
        }
    }

    /* If the largest component is negative, negate the entire quaternion
     * so the reconstructed (dropped) component is always positive. */
    float sign = (components[max_idx] < 0.0f) ? -1.0f : 1.0f;

    /* Collect the 3 remaining components in order. */
    float remaining[3];
    int ri = 0;
    for (int i = 0; i < 4; i++) {
        if (i == max_idx) continue;
        remaining[ri++] = components[i] * sign;
    }

    /* Encode each remaining component as snorm16. */
    for (int i = 0; i < 3; i++) {
        float clamped = fr_clampf(remaining[i], -1.0f, 1.0f);
        out[i] = (int16_t)roundf(clamped * SNORM16_MAX);
    }

    /* Store the max component index in the high 2 bits of out[0].
     * Clear the high 2 bits first, then set them. The snorm16 value
     * uses bits 0-13, and the index uses bits 14-15. */
    out[0] = (int16_t)((out[0] & 0x3FFF) | (max_idx << 14));
}

phys_quat_t phys_dequantize_quat(const int16_t in[3])
{
    /* Extract the max component index from high 2 bits of in[0]. */
    int max_idx = (in[0] >> 14) & 0x03;

    /* Decode the 3 snorm16 values, masking off the index bits from in[0]. */
    float remaining[3];
    int16_t val0 = (int16_t)((in[0] & 0x3FFF) | ((in[0] & 0x2000) ? 0xC000 : 0));
    remaining[0] = (float)val0 / SNORM16_MAX;
    remaining[1] = (float)in[1] / SNORM16_MAX;
    remaining[2] = (float)in[2] / SNORM16_MAX;

    /* Reconstruct the dropped component. */
    float sum_sq = remaining[0] * remaining[0]
                 + remaining[1] * remaining[1]
                 + remaining[2] * remaining[2];
    float dropped = (sum_sq < 1.0f) ? sqrtf(1.0f - sum_sq) : 0.0f;

    /* Reassemble the quaternion components. */
    float result[4];
    int ri = 0;
    for (int i = 0; i < 4; i++) {
        if (i == max_idx) {
            result[i] = dropped;
        } else {
            result[i] = remaining[ri++];
        }
    }

    return (phys_quat_t){result[0], result[1], result[2], result[3]};
}
