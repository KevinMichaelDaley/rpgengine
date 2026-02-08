#ifndef FERRUM_NET_REPLICATION_QUAT_SMALLEST3_H
#define FERRUM_NET_REPLICATION_QUAT_SMALLEST3_H

#include <stdint.h>

/** @file
 * @brief Smallest-three quaternion compression for wire format.
 *
 * Encodes a unit quaternion in 7 bytes (56 bits):
 *   - 2 bits: index of the largest component (dropped)
 *   - 3 × 18 bits: the three remaining components as signed fixed-point
 *     in [-1/√2, 1/√2] mapped to [-131071, 131071]
 *
 * The dropped component is reconstructed as:
 *   q[largest] = sqrt(1 - a² - b² - c²)
 *
 * Since the largest component is always positive (we negate the quat
 * if necessary before encoding), no sign bit is needed for it.
 *
 * Precision: ~0.0076 milliradians per component (18-bit resolution
 * over ±0.7071 range).
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Wire size of the smallest-three encoded quaternion. */
#define NET_REPL_QUAT_S3_WIRE_SIZE 7u

/** In-memory representation before/after encode/decode.
 *  The application works with float quaternions; this struct is the
 *  intermediate packed form for serialization helpers. */
typedef struct net_repl_quat_s3 {
    uint8_t largest_index;  /**< 0-3: which component was dropped. */
    int32_t a;              /**< First remaining component (fixed-point). */
    int32_t b;              /**< Second remaining component. */
    int32_t c;              /**< Third remaining component. */
} net_repl_quat_s3_t;

/**
 * @brief Compress a float quaternion to smallest-three form.
 *
 * @param qx,qy,qz,qw  Input unit quaternion (need not be perfectly
 *                       normalized; will be treated as-is).
 * @param out            Packed result.
 */
void net_repl_quat_s3_pack(float qx, float qy, float qz, float qw,
                           net_repl_quat_s3_t *out);

/**
 * @brief Decompress smallest-three form back to a float quaternion.
 *
 * @param s3              Packed quaternion.
 * @param qx,qy,qz,qw    Output floats.
 */
void net_repl_quat_s3_unpack(const net_repl_quat_s3_t *s3,
                             float *qx, float *qy, float *qz, float *qw);

/**
 * @brief Write a packed quaternion to a 7-byte wire buffer.
 *
 * Layout (big-endian, 56 bits):
 *   bits 55-54: largest_index (2 bits)
 *   bits 53-36: a (18 bits, signed)
 *   bits 35-18: b (18 bits, signed)
 *   bits 17-0:  c (18 bits, signed)
 */
void net_repl_quat_s3_write(const net_repl_quat_s3_t *s3, uint8_t *out);

/**
 * @brief Read a packed quaternion from a 7-byte wire buffer.
 */
void net_repl_quat_s3_read(const uint8_t *buf, net_repl_quat_s3_t *s3);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_NET_REPLICATION_QUAT_SMALLEST3_H */
