#ifndef FERRUM_NET_QUANTIZATION_H
#define FERRUM_NET_QUANTIZATION_H

/** @file
 * @brief Deterministic quantization helpers for networking.
 *
 * These helpers are intended for stable, replayable tests and predictable
 * replication behavior.
 */

#include <stdint.h>

#include "ferrum/math/quat.h"
#include "ferrum/math/vec3.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Success. */
#define NET_QUANT_OK 0
/** Invalid arguments or invalid encoded value. */
#define NET_QUANT_ERR_INVALID -1
/** Value is out of representable range. */
#define NET_QUANT_ERR_RANGE -2

/**
 * Quantized vec3 in millimeters (int32).
 *
 * Representable range (meters):
 *  - min: -2147483648 mm / 1000
 *  - max:  2147483647 mm / 1000
 */
typedef struct net_qvec3_mm {
    int32_t x_mm;
    int32_t y_mm;
    int32_t z_mm;
    uint32_t _magic;
} net_qvec3_mm_t;

/**
 * Quantized quaternion as signed normalized int16 components (snorm16).
 *
 * Components are mapped to the range [-1, 1] using a scale factor of 32767.
 */
typedef struct net_qquat_snorm16 {
    int16_t x;
    int16_t y;
    int16_t z;
    int16_t w;
    uint32_t _magic;
} net_qquat_snorm16_t;

/**
 * @brief Quantize a vec3 to millimeters with deterministic rounding.
 *
 * Rounding semantics: halves round away from zero (e.g. 0.0005 -> 1 mm, -0.0005 -> -1 mm).
 *
 * Out-of-range values are rejected with NET_QUANT_ERR_RANGE (no implicit
 * clamping).
 *
 * @param v Input vector (meters).
 * @param out Output quantized value (non-NULL).
 * @return NET_QUANT_OK, NET_QUANT_ERR_INVALID, or NET_QUANT_ERR_RANGE.
 */
int net_quantize_vec3_mm(vec3_t v, net_qvec3_mm_t *out);

/**
 * @brief Dequantize a vec3 from millimeters to meters.
 *
 * @param q Quantized value previously produced by net_quantize_vec3_mm.
 * @param out Output vector (non-NULL).
 * @return NET_QUANT_OK or NET_QUANT_ERR_INVALID.
 */
int net_dequantize_vec3_mm(net_qvec3_mm_t q, vec3_t *out);

/**
 * @brief Add two u16 animation time values with wraparound.
 */
uint16_t net_anim_time_u16_add_wrap(uint16_t t, uint16_t delta);

/**
 * @brief Compute signed delta (a - b) in u16 time domain, choosing the shortest wrap direction.
 */
int net_anim_time_u16_delta_signed(uint16_t a, uint16_t b);

/**
 * @brief Quantize a quaternion to snorm16 (deterministic), canonicalizing sign to w >= 0.
 *
 * The input quaternion is normalized. Components are clamped to [-1, 1] before
 * mapping to snorm16 to avoid overflow from floating point edge cases.
 *
 * @param q Input quaternion.
 * @param out Output packed quaternion (non-NULL).
 * @return NET_QUANT_OK or NET_QUANT_ERR_INVALID.
 */
int net_quantize_quat_snorm16(quat_t q, net_qquat_snorm16_t *out);

/**
 * @brief Dequantize a snorm16 quaternion, normalizing and canonicalizing sign to w >= 0.
 *
 * @param q Packed quaternion previously produced by net_quantize_quat_snorm16.
 * @param out Output quaternion (non-NULL).
 * @return NET_QUANT_OK or NET_QUANT_ERR_INVALID.
 */
int net_dequantize_quat_snorm16(net_qquat_snorm16_t q, quat_t *out);

/**
 * @brief Convert a float32 to IEEE 754 binary16 (half-precision).
 *
 * Range: ±65504 with ~3 significant digits.  Values exceeding
 * float16 max are clamped to ±65504.  Very small values flush to
 * ±0.  Rounding: nearest, ties to even.
 *
 * @param f  Input float32 value.
 * @return   The float16 bit pattern stored in a uint16_t.
 */
uint16_t net_float16_from_float(float f);

/**
 * @brief Convert an IEEE 754 binary16 (half-precision) to float32.
 *
 * Exact conversion — every float16 value is exactly representable
 * in float32.
 *
 * @param h  The float16 bit pattern in a uint16_t.
 * @return   The corresponding float32 value.
 */
float net_float16_to_float(uint16_t h);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_NET_QUANTIZATION_H */
