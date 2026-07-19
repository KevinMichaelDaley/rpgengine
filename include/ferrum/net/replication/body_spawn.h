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

/** Wire size: 2+1+1+4+12+7+6+6 = 39 bytes. */
#define NET_REPL_BODY_SPAWN_PAYLOAD_SIZE 39u

/**
 * @brief BODY_SPAWN carries a body's full collider PRIMITIVE (not for client
 *        collision -- client physics stays integration-only -- but so the client
 *        can build a posed gi_collider proxy for dynamic-SDF injection, rpg-b5r3).
 *
 * shape_type codes (match fr_collider_prim_kind_t):
 *   0=box       half_x/y/z = half-extents
 *   1=sphere    radius = half_x
 *   2=capsule   radius = half_x, half_height = half_y (segment along local Y)
 *   3=mesh      geometry over MESH_DATA keyed by body_id
 *   4=halfspace normal = (half_x,half_y,half_z) unit vector, distance = off_x
 *   5=convex    hull points over MESH_DATA keyed by body_id
 *   6=compound  convex decomposition over MESH_DATA keyed by body_id
 *   7=point     offset only
 * off_x/y/z is the collider's local offset from the body origin (meters).
 */
typedef struct net_repl_body_spawn {
    uint16_t body_id;            /**< Physics body index. */
    uint8_t  flags;              /**< Body flags (kinematic, static, etc). */
    uint8_t  shape_type;         /**< Collider kind (see codes above). */
    uint32_t color_seed;         /**< Color seed for rendering. */
    net_repl_vec3_mm_t pos_mm;   /**< Initial position in millimeters. */
    float    rot_x;              /**< Initial orientation (unpacked quat). */
    float    rot_y;
    float    rot_z;
    float    rot_w;
    uint16_t half_x_f16;          /**< Shape half-extent / radius / normal.x (f16, m). */
    uint16_t half_y_f16;          /**< Shape half-extent / half_height / normal.y (f16, m). */
    uint16_t half_z_f16;          /**< Shape half-extent / normal.z (f16, m). */
    uint16_t off_x_f16;           /**< Collider local offset X (f16, m); halfspace: distance. */
    uint16_t off_y_f16;           /**< Collider local offset Y (f16, m). */
    uint16_t off_z_f16;           /**< Collider local offset Z (f16, m). */
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
