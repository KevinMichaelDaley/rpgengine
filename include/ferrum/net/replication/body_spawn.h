#ifndef FERRUM_NET_REPLICATION_BODY_SPAWN_H
#define FERRUM_NET_REPLICATION_BODY_SPAWN_H

#include <stddef.h>
#include <stdint.h>

#include "ferrum/net/replication/common.h"
#include "ferrum/net/replication/quat_smallest3.h"
#include "ferrum/net/replication/vec3_mm.h"

/** @file
 * @brief BODY_SPAWN message: server -> client (reliable).
 *
 * Sent once per body per client to introduce a new physics body.
 * Contains shape, color, and initial position so the client can
 * create a renderable entity.  Subsequent per-tick updates use
 * BODY_STATE (unreliable).
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Wire size: 2+1+1+4+12+7+6 = 33 bytes. */
#define NET_REPL_BODY_SPAWN_PAYLOAD_SIZE 33u

typedef struct net_repl_body_spawn {
    uint16_t body_id;            /**< Physics body index. */
    uint8_t  flags;              /**< Body flags (kinematic, static, etc). */
    uint8_t  shape_type;         /**< 0=box, 1=sphere, 2=capsule, 3=mesh, 4=halfspace. */
    uint32_t color_seed;         /**< Color seed for rendering. */
    net_repl_vec3_mm_t pos_mm;   /**< Initial position in millimeters. */
    float    rot_x;              /**< Initial orientation (unpacked quat). */
    float    rot_y;
    float    rot_z;
    float    rot_w;
    uint16_t half_x_f16;          /**< Shape half-extent X (float16, meters). */
    uint16_t half_y_f16;          /**< Shape half-extent Y (float16, meters). */
    uint16_t half_z_f16;          /**< Shape half-extent Z (float16, meters). */
} net_repl_body_spawn_t;

/**
 * @brief Encode a BODY_SPAWN message to wire format.
 * @param msg  Source message. Must not be NULL.
 * @param out  Output buffer. Must be >= NET_REPL_BODY_SPAWN_PAYLOAD_SIZE.
 * @param out_size  Size of output buffer.
 * @return NET_REPL_OK on success.
 */
int net_repl_body_spawn_encode(const net_repl_body_spawn_t *msg,
                               uint8_t *out, size_t out_size);

/**
 * @brief Decode a BODY_SPAWN message from wire format.
 * @param msg  Destination message. Must not be NULL.
 * @param payload  Input buffer.
 * @param payload_size  Size of input buffer.
 * @return NET_REPL_OK on success.
 */
int net_repl_body_spawn_decode(net_repl_body_spawn_t *msg,
                               const uint8_t *payload, size_t payload_size);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_NET_REPLICATION_BODY_SPAWN_H */
