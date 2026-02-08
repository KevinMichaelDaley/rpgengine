/**
 * @file quat_smallest3.c
 * @brief Smallest-three quaternion compression.
 *
 * Encodes a unit quaternion in 7 bytes by dropping the largest
 * component (reconstructed via sqrt) and packing the remaining
 * three as 18-bit signed fixed-point values.
 */

#include "ferrum/net/replication/quat_smallest3.h"

#include <math.h>

/** The three kept components lie in [-1/√2, 1/√2].
 *  We map this range to [-131071, 131071] (18-bit signed). */
#define S3_SCALE 131071.0f
#define S3_RANGE 0.7071067811865476f  /* 1/sqrt(2) */

/* ── Pack / Unpack ─────────────────────────────────────────────── */

void net_repl_quat_s3_pack(float qx, float qy, float qz, float qw,
                           net_repl_quat_s3_t *out) {
    /* Find the largest-magnitude component. */
    float abs_vals[4] = {
        fabsf(qx), fabsf(qy), fabsf(qz), fabsf(qw)
    };
    uint8_t largest = 0;
    if (abs_vals[1] > abs_vals[largest]) largest = 1;
    if (abs_vals[2] > abs_vals[largest]) largest = 2;
    if (abs_vals[3] > abs_vals[largest]) largest = 3;

    /* Negate the entire quaternion if the largest component is negative
     * so we can always reconstruct with a positive sqrt. */
    float q[4] = {qx, qy, qz, qw};
    if (q[largest] < 0.0f) {
        q[0] = -q[0]; q[1] = -q[1]; q[2] = -q[2]; q[3] = -q[3];
    }

    /* Extract the three remaining components in order. */
    float kept[3];
    int ki = 0;
    for (int i = 0; i < 4; i++) {
        if (i != (int)largest) {
            kept[ki++] = q[i];
        }
    }

    /* Quantize to 18-bit signed fixed-point. */
    out->largest_index = largest;
    out->a = (int32_t)roundf(kept[0] / S3_RANGE * S3_SCALE);
    out->b = (int32_t)roundf(kept[1] / S3_RANGE * S3_SCALE);
    out->c = (int32_t)roundf(kept[2] / S3_RANGE * S3_SCALE);

    /* Clamp to 18-bit signed range. */
    if (out->a >  131071) out->a =  131071;
    if (out->a < -131071) out->a = -131071;
    if (out->b >  131071) out->b =  131071;
    if (out->b < -131071) out->b = -131071;
    if (out->c >  131071) out->c =  131071;
    if (out->c < -131071) out->c = -131071;
}

void net_repl_quat_s3_unpack(const net_repl_quat_s3_t *s3,
                             float *qx, float *qy, float *qz, float *qw) {
    /* Dequantize the three kept components. */
    float kept[3];
    kept[0] = (float)s3->a / S3_SCALE * S3_RANGE;
    kept[1] = (float)s3->b / S3_SCALE * S3_RANGE;
    kept[2] = (float)s3->c / S3_SCALE * S3_RANGE;

    /* Reconstruct the dropped component. */
    float sum_sq = kept[0] * kept[0] + kept[1] * kept[1] + kept[2] * kept[2];
    float largest_val = (sum_sq < 1.0f) ? sqrtf(1.0f - sum_sq) : 0.0f;

    /* Place components back. */
    float q[4];
    int ki = 0;
    for (int i = 0; i < 4; i++) {
        if (i == (int)s3->largest_index) {
            q[i] = largest_val;
        } else {
            q[i] = kept[ki++];
        }
    }

    *qx = q[0];
    *qy = q[1];
    *qz = q[2];
    *qw = q[3];
}

/* ── Wire read / write (7 bytes, big-endian bit packing) ───────── */

void net_repl_quat_s3_write(const net_repl_quat_s3_t *s3, uint8_t *out) {
    /* Pack 2 + 18 + 18 + 18 = 56 bits into 7 bytes.
     *
     * Byte layout (MSB first):
     *   [0]: largest(2) | a[17..12](6)
     *   [1]: a[11..4](8)
     *   [2]: a[3..0](4) | b[17..14](4)
     *   [3]: b[13..6](8)
     *   [4]: b[5..0](6) | c[17..16](2)
     *   [5]: c[15..8](8)
     *   [6]: c[7..0](8)
     */
    uint32_t ua = (uint32_t)(s3->a & 0x3FFFF);  /* 18-bit mask */
    uint32_t ub = (uint32_t)(s3->b & 0x3FFFF);
    uint32_t uc = (uint32_t)(s3->c & 0x3FFFF);

    out[0] = (uint8_t)((s3->largest_index << 6) | ((ua >> 12) & 0x3Fu));
    out[1] = (uint8_t)((ua >> 4) & 0xFFu);
    out[2] = (uint8_t)(((ua & 0xFu) << 4) | ((ub >> 14) & 0xFu));
    out[3] = (uint8_t)((ub >> 6) & 0xFFu);
    out[4] = (uint8_t)(((ub & 0x3Fu) << 2) | ((uc >> 16) & 0x3u));
    out[5] = (uint8_t)((uc >> 8) & 0xFFu);
    out[6] = (uint8_t)(uc & 0xFFu);
}

void net_repl_quat_s3_read(const uint8_t *buf, net_repl_quat_s3_t *s3) {
    s3->largest_index = (uint8_t)((buf[0] >> 6) & 0x3u);

    uint32_t ua = ((uint32_t)(buf[0] & 0x3Fu) << 12) |
                  ((uint32_t)buf[1] << 4) |
                  ((uint32_t)(buf[2] >> 4) & 0xFu);

    uint32_t ub = ((uint32_t)(buf[2] & 0xFu) << 14) |
                  ((uint32_t)buf[3] << 6) |
                  ((uint32_t)(buf[4] >> 2) & 0x3Fu);

    uint32_t uc = ((uint32_t)(buf[4] & 0x3u) << 16) |
                  ((uint32_t)buf[5] << 8) |
                  (uint32_t)buf[6];

    /* Sign-extend from 18 bits. */
    if (ua & 0x20000u) ua |= 0xFFFC0000u;
    if (ub & 0x20000u) ub |= 0xFFFC0000u;
    if (uc & 0x20000u) uc |= 0xFFFC0000u;

    s3->a = (int32_t)ua;
    s3->b = (int32_t)ub;
    s3->c = (int32_t)uc;
}
