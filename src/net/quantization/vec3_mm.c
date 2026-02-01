#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "ferrum/net/quantization.h"

#define NET_QVEC3_MM_MAGIC 0x4D4D3351u /* 'Q3MM' */

static int32_t round_half_away_from_zero_to_i32(double x) {
    if (x >= 0.0) {
        return (int32_t)floor(x + 0.5);
    }
    return (int32_t)ceil(x - 0.5);
}

int net_quantize_vec3_mm(vec3_t v, net_qvec3_mm_t *out) {
    if (out == NULL) {
        return NET_QUANT_ERR_INVALID;
    }

    if (!isfinite(v.x) || !isfinite(v.y) || !isfinite(v.z)) {
        return NET_QUANT_ERR_INVALID;
    }

    const double sx = (double)v.x * 1000.0;
    const double sy = (double)v.y * 1000.0;
    const double sz = (double)v.z * 1000.0;

    if (sx > (double)INT32_MAX || sx < (double)INT32_MIN) {
        return NET_QUANT_ERR_RANGE;
    }
    if (sy > (double)INT32_MAX || sy < (double)INT32_MIN) {
        return NET_QUANT_ERR_RANGE;
    }
    if (sz > (double)INT32_MAX || sz < (double)INT32_MIN) {
        return NET_QUANT_ERR_RANGE;
    }

    out->x_mm = round_half_away_from_zero_to_i32(sx);
    out->y_mm = round_half_away_from_zero_to_i32(sy);
    out->z_mm = round_half_away_from_zero_to_i32(sz);
    out->_magic = NET_QVEC3_MM_MAGIC;
    return NET_QUANT_OK;
}

int net_dequantize_vec3_mm(net_qvec3_mm_t q, vec3_t *out) {
    if (out == NULL) {
        return NET_QUANT_ERR_INVALID;
    }
    if (q._magic != NET_QVEC3_MM_MAGIC) {
        return NET_QUANT_ERR_INVALID;
    }

    out->x = (float)q.x_mm / 1000.0f;
    out->y = (float)q.y_mm / 1000.0f;
    out->z = (float)q.z_mm / 1000.0f;
    return NET_QUANT_OK;
}
