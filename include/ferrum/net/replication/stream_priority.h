/**
 * @file stream_priority.h
 * @brief STREAM_PRIORITY message: server -> client (reliable), rpg-3ldk.
 *
 * The server assigns per-client streaming priority over the level's asset/chunk
 * ids (computed from the player's position/interest -- nearest chunks first) and
 * sends the hints so the client reorders its asset streamer's request queue
 * (rpg-nbp2). A batch of (asset id, priority) entries; higher priority streams
 * first. Sent on join and as the player moves.
 */
#ifndef FERRUM_NET_REPLICATION_STREAM_PRIORITY_H
#define FERRUM_NET_REPLICATION_STREAM_PRIORITY_H

#include <stddef.h>
#include <stdint.h>

#include "ferrum/net/replication/common.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Max priority entries per message. */
#define NET_REPL_STREAM_PRIORITY_MAX 64u
/** Per-entry wire size: id (u64) + priority (i32) = 12 bytes. */
#define NET_REPL_STREAM_PRIORITY_ENTRY_SIZE 12u
/** Header wire size: count (u16). */
#define NET_REPL_STREAM_PRIORITY_HEADER_SIZE 2u
/** Max payload = header + MAX entries. */
#define NET_REPL_STREAM_PRIORITY_MAX_PAYLOAD \
    (NET_REPL_STREAM_PRIORITY_HEADER_SIZE + \
     NET_REPL_STREAM_PRIORITY_MAX * NET_REPL_STREAM_PRIORITY_ENTRY_SIZE)

/** One (asset id -> streaming priority) hint. */
typedef struct net_repl_stream_prio_entry {
    uint64_t id;        /**< asset / chunk id. */
    int32_t  priority;  /**< higher streams first. */
} net_repl_stream_prio_entry_t;

/** A batch of streaming-priority hints. */
typedef struct net_repl_stream_priority {
    uint16_t count;
    net_repl_stream_prio_entry_t entries[NET_REPL_STREAM_PRIORITY_MAX];
} net_repl_stream_priority_t;

/**
 * @brief Encode a STREAM_PRIORITY message. @c count is clamped to the max.
 * @param out_size  buffer size (>= header + count*entry).
 * @param written   receives the number of bytes written (may be NULL).
 * @return NET_REPL_OK, or NET_REPL_ERR_INVALID / NET_REPL_ERR_SHORT.
 */
int net_repl_stream_priority_encode(const net_repl_stream_priority_t *msg,
                                    uint8_t *out, size_t out_size, size_t *written);

/**
 * @brief Decode a STREAM_PRIORITY message.
 * @return NET_REPL_OK, or NET_REPL_ERR_INVALID / NET_REPL_ERR_SHORT (truncated
 *         or count exceeds the max).
 */
int net_repl_stream_priority_decode(net_repl_stream_priority_t *msg,
                                    const uint8_t *payload, size_t payload_size);

struct fr_asset_stream; /* ferrum/asset/asset_stream.h */

/**
 * @brief Apply a decoded STREAM_PRIORITY batch to the client's asset streamer:
 *        set each known asset's priority so its request queue reorders.
 * @return number of entries applied (matched a registered asset).
 */
uint32_t net_repl_stream_priority_apply(const net_repl_stream_priority_t *msg,
                                        struct fr_asset_stream *stream);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_NET_REPLICATION_STREAM_PRIORITY_H */
