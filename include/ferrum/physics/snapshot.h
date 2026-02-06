#ifndef FERRUM_PHYSICS_SNAPSHOT_H
#define FERRUM_PHYSICS_SNAPSHOT_H

/**
 * @file snapshot.h
 * @brief Network snapshot encoding/decoding for physics state replication.
 *
 * Provides quantized body snapshots for compact wire-format transmission
 * and functions to encode/decode full world state.
 */

#include <stdint.h>
#include <stddef.h>

#include "ferrum/physics/phys_types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct phys_world;

/**
 * @brief Quantized physics body for network transmission (26 bytes).
 *
 * Each body's continuous state is quantized to int16 for compact
 * wire format.  Position and velocity use millimeter-scale quantization;
 * orientation uses smallest-3 quaternion encoding.
 */
typedef struct phys_snapshot_body {
    int16_t position[3];     /**< Position quantized to mm (±32m range). */
    int16_t orientation[3];  /**< Smallest-3 quaternion (snorm16). */
    int16_t linear_vel[3];   /**< Velocity quantized (±32 m/s range). */
    int16_t angular_vel[3];  /**< Angular velocity quantized. */
    uint8_t flags;           /**< Body flags (sleeping, kinematic, etc). */
    uint8_t tier;            /**< Simulation tier. */
} phys_snapshot_body_t;

/**
 * @brief Complete physics snapshot for one frame.
 *
 * Ownership: the bodies pointer must be caller-allocated or
 * arena-allocated before calling phys_snapshot_decode().
 */
typedef struct phys_snapshot {
    uint64_t tick;               /**< World tick at time of snapshot. */
    uint32_t body_count;         /**< Number of bodies in snapshot. */
    phys_snapshot_body_t *bodies; /**< Caller-allocated body array. */
} phys_snapshot_t;

/* ── Quantization ─────────────────────────────────────────────── */

/**
 * @brief Quantize a vec3 by scale, clamping to int16 range.
 *
 * @param v      Input vector.
 * @param out    Output array of 3 int16 values (non-NULL).
 * @param scale  Quantization scale (e.g., 1000.0 for mm precision).
 *
 * Side effects: writes to out[0..2].
 */
void phys_quantize_vec3(phys_vec3_t v, int16_t out[3], float scale);

/**
 * @brief Dequantize 3 int16 values back to a vec3.
 *
 * @param in        Input array of 3 int16 values (non-NULL).
 * @param inv_scale Inverse of quantization scale (e.g., 1/1000.0).
 * @return Reconstructed vector.
 */
phys_vec3_t phys_dequantize_vec3(const int16_t in[3], float inv_scale);

/**
 * @brief Quantize a quaternion using smallest-3 encoding.
 *
 * Finds the component with the largest absolute value, encodes
 * the other 3 as snorm16.  The index of the largest component is
 * stored in the high 2 bits of out[0].
 *
 * @param q    Input quaternion (should be normalized).
 * @param out  Output array of 3 int16 values (non-NULL).
 *
 * Side effects: writes to out[0..2].
 */
void phys_quantize_quat(phys_quat_t q, int16_t out[3]);

/**
 * @brief Dequantize a smallest-3 encoded quaternion.
 *
 * Reconstructs the 4th component via sqrt(1 - sum_of_squares).
 *
 * @param in  Input array of 3 int16 values (non-NULL).
 * @return Reconstructed quaternion.
 */
phys_quat_t phys_dequantize_quat(const int16_t in[3]);

/* ── Snapshot encode/decode ───────────────────────────────────── */

/**
 * @brief Encode the current world state into a binary buffer.
 *
 * Wire format: [tick:8][body_count:4][body_0:26][body_1:26]...
 *
 * @param world    World to snapshot (NULL returns 0).
 * @param buffer   Output buffer (NULL returns 0).
 * @param max_size Buffer capacity in bytes.
 * @return Number of bytes written, or 0 on error / insufficient space.
 *
 * Ownership: caller owns the buffer.
 * Side effects: none (read-only on world).
 */
size_t phys_snapshot_encode(const struct phys_world *world,
                            uint8_t *buffer, size_t max_size);

/**
 * @brief Decode a binary buffer into a snapshot structure.
 *
 * The caller must pre-allocate snapshot_out->bodies with enough
 * space for the body count encoded in the buffer.
 *
 * @param buffer       Input buffer (NULL returns -1).
 * @param size         Buffer size in bytes.
 * @param snapshot_out Output snapshot (NULL returns -1).
 * @return 0 on success, -1 on error.
 *
 * Ownership: caller owns snapshot_out and its bodies array.
 */
int phys_snapshot_decode(const uint8_t *buffer, size_t size,
                         phys_snapshot_t *snapshot_out);

/* ── Snapshot apply to world ──────────────────────────────────── */

/**
 * @brief Apply a decoded snapshot to a world, overwriting body state.
 *
 * Dequantizes snapshot bodies and writes position, orientation,
 * velocity, flags, and tier to the world's body pool.
 *
 * @param world    Target world (NULL returns -1).
 * @param snapshot Snapshot to apply (NULL returns -1).
 * @return 0 on success, -1 on error.
 *
 * Side effects: modifies world body pool state.
 */
int phys_snapshot_apply(struct phys_world *world,
                        const phys_snapshot_t *snapshot);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_SNAPSHOT_H */
