/**
 * @file phys_cmd_push.c
 * @brief Push a physics command into a topic channel.
 *
 * Serializes a 1-byte type tag followed by the command struct payload.
 */

#include "ferrum/physics/phys_cmd.h"
#include "ferrum/net/topic_channel.h"

#include <string.h>

/* ── Public API (1 non-static function) ────────────────────────── */

bool phys_cmd_push(fr_topic_channel_t *cmd_channel,
                   phys_cmd_type_t type,
                   const void *payload, size_t payload_size) {
    if (!cmd_channel || !payload || payload_size == 0) {
        return false;
    }

    /* Serialize: [1 byte type][payload bytes]. */
    uint8_t buf[256];
    if (1u + payload_size > sizeof(buf)) {
        return false;
    }

    buf[0] = (uint8_t)type;
    memcpy(buf + 1u, payload, payload_size);

    return fr_topic_channel_push(cmd_channel, buf, 1u + payload_size);
}
