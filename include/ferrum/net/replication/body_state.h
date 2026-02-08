#ifndef FERRUM_NET_REPLICATION_BODY_STATE_H
#define FERRUM_NET_REPLICATION_BODY_STATE_H

#include <stddef.h>
#include <stdint.h>

#include "ferrum/net/replication/common.h"
#include "ferrum/net/replication/vec3_mm.h"

/** @file
 * @brief BODY_STATE message: server -> client (unreliable).
 *
 * Per-tick position, orientation, and velocity update for a physics
 * body the client already knows about (via BODY_SPAWN).
 *
 * Orientation uses smallest-three quaternion compression (7 bytes).
 * Linear velocity is quantized to mm/s as int16 (±32 m/s range).
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Wire size: 2+2+12+7+6+6 = 35 bytes. */
#define NET_REPL_BODY_STATE_PAYLOAD_SIZE 35u

typedef struct net_repl_body_state {
    uint16_t server_tick;        /**< Server tick counter. */
    uint16_t body_id;            /**< Physics body index. */
    net_repl_vec3_mm_t pos_mm;   /**< Position in millimeters. */

    /** Orientation as smallest-three quaternion (application-level
     *  floats; packed to 7 wire bytes by encode/decode). */
    float rot_x;
    float rot_y;
    float rot_z;
    float rot_w;

    /** Linear velocity in mm/s, clamped to ±32767 (~±32 m/s). */
    int16_t vel_x_mm_s;
    int16_t vel_y_mm_s;
    int16_t vel_z_mm_s;

    /** Angular velocity in mrad/s, clamped to ±32767 (~±32 rad/s). */
    int16_t ang_x_mrad_s;
    int16_t ang_y_mrad_s;
    int16_t ang_z_mrad_s;
} net_repl_body_state_t;

/**
 * @brief Encode a BODY_STATE message to wire format.
 * @return NET_REPL_OK on success.
 */
int net_repl_body_state_encode(const net_repl_body_state_t *msg,
                               uint8_t *out, size_t out_size);

/**
 * @brief Decode a BODY_STATE message from wire format.
 * @return NET_REPL_OK on success.
 */
int net_repl_body_state_decode(net_repl_body_state_t *msg,
                               const uint8_t *payload, size_t payload_size);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_NET_REPLICATION_BODY_STATE_H */
