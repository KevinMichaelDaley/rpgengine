#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "ferrum/net/quantization.h"

#define NET_QQUAT_SNORM16_MAGIC 0x4E513136u /* 'NQ16' */

static int16_t clamp_round_snorm16(float x) {
    if (x > 1.0f) {
        x = 1.0f;
    } else if (x < -1.0f) {
        x = -1.0f;
    }

    const double scaled = (double)x * 32767.0;
    if (scaled >= 0.0) {
        return (int16_t)floor(scaled + 0.5);
    }
    return (int16_t)ceil(scaled - 0.5);
}

static int quat_is_finite(quat_t q) {
    return isfinite(q.x) && isfinite(q.y) && isfinite(q.z) && isfinite(q.w);
}

int net_quantize_quat_snorm16(quat_t q, net_qquat_snorm16_t *out) {
    if (out == NULL) {
        return NET_QUANT_ERR_INVALID;
    }
    if (!quat_is_finite(q)) {
        return NET_QUANT_ERR_INVALID;
    }

    const float len_sq = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    if (!(len_sq > 0.0f)) {
        return NET_QUANT_ERR_INVALID;
    }

    const float inv_len = 1.0f / sqrtf(len_sq);
    q.x *= inv_len;
    q.y *= inv_len;
    q.z *= inv_len;
    q.w *= inv_len;

    if (q.w < 0.0f) {
        q.x = -q.x;
        q.y = -q.y;
        q.z = -q.z;
        q.w = -q.w;
    }

    out->x = clamp_round_snorm16(q.x);
    out->y = clamp_round_snorm16(q.y);
    out->z = clamp_round_snorm16(q.z);
    out->w = clamp_round_snorm16(q.w);
    out->_magic = NET_QQUAT_SNORM16_MAGIC;

    return NET_QUANT_OK;
}

int net_dequantize_quat_snorm16(net_qquat_snorm16_t q, quat_t *out) {
    if (out == NULL) {
        return NET_QUANT_ERR_INVALID;
    }
    if (q._magic != NET_QQUAT_SNORM16_MAGIC) {
        return NET_QUANT_ERR_INVALID;
    }

    quat_t r;
    r.x = (float)q.x / 32767.0f;
    r.y = (float)q.y / 32767.0f;
    r.z = (float)q.z / 32767.0f;
    r.w = (float)q.w / 32767.0f;

    const float len_sq = r.x * r.x + r.y * r.y + r.z * r.z + r.w * r.w;
    if (!(len_sq > 0.0f)) {
        return NET_QUANT_ERR_INVALID;
    }

    const float inv_len = 1.0f / sqrtf(len_sq);
    r.x *= inv_len;
    r.y *= inv_len;
    r.z *= inv_len;
    r.w *= inv_len;

    if (r.w < 0.0f) {
        r.x = -r.x;
        r.y = -r.y;
        r.z = -r.z;
        r.w = -r.w;
    }

    *out = r;
    return NET_QUANT_OK;
}
