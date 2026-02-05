#ifndef FERRUM_NET_REPLICATION_INPUT_ROT_H
#define FERRUM_NET_REPLICATION_INPUT_ROT_H

#include <stddef.h>
#include <stdint.h>

#include "ferrum/net/replication/common.h"

/** @file
 * @brief INPUT-ROT message: client -> server (reliable).
 *
 * This represents a player input event that changes an entity's intended angular
 * velocity. The server treats this input as authoritative for that entity.
 */

#ifdef __cplusplus
extern "C" {
#endif

#define NET_REPL_INPUT_ROT_PAYLOAD_SIZE 16u

typedef struct net_repl_input_rot {
    uint32_t entity_id;
    uint32_t event_id;

    /* Axis encoded as snorm16 components (range [-32767,32767]). */
    int16_t axis_x_snorm16;
    int16_t axis_y_snorm16;
    int16_t axis_z_snorm16;

    /* Angular speed in milliradians per second. */
    uint16_t speed_millirad_per_s;
} net_repl_input_rot_t;

int net_repl_input_rot_encode(const net_repl_input_rot_t *msg, uint8_t *out_payload, size_t out_size);
int net_repl_input_rot_decode(net_repl_input_rot_t *msg, const uint8_t *payload, size_t payload_size);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_NET_REPLICATION_INPUT_ROT_H */
