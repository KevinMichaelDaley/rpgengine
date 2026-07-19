/**
 * @file stream_priority_apply.c
 * @brief Apply a decoded STREAM_PRIORITY batch to the asset streamer (rpg-3ldk).
 *        Glue between the net message and the streamer's priority-owned queue.
 */
#include "ferrum/net/replication/stream_priority.h"
#include "ferrum/asset/asset_stream.h"

uint32_t net_repl_stream_priority_apply(const net_repl_stream_priority_t *msg,
                                        struct fr_asset_stream *stream)
{
    if (msg == NULL || stream == NULL) return 0u;
    uint32_t applied = 0;
    for (uint16_t i = 0; i < msg->count; ++i) {
        if (fr_asset_stream_set_priority(stream, msg->entries[i].id,
                                         msg->entries[i].priority))
            ++applied;
    }
    return applied;
}
