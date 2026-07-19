/**
 * @file server_stream_priority.h
 * @brief Server-side computation of per-client streaming priority (rpg-3ldk):
 *        rank the level's asset/chunk ids by distance from a player, nearest
 *        first, into a STREAM_PRIORITY message to send to that client. Headless.
 */
#ifndef FERRUM_SERVER_SERVER_STREAM_PRIORITY_H
#define FERRUM_SERVER_SERVER_STREAM_PRIORITY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

struct net_repl_stream_priority; /* ferrum/net/replication/stream_priority.h */

/**
 * @brief Fill a STREAM_PRIORITY message: priority = -(dist^2 * scale) from
 *        @p player to each chunk's world box (nearer => higher priority). Entries
 *        are capped at the message max.
 *
 * @param ids       [n] asset/chunk ids.
 * @param box_min   [n*3] chunk box mins.
 * @param box_max   [n*3] chunk box maxs.
 * @param n         chunk count.
 * @param player    player position (interest point).
 * @param scale     world-distance^2 -> priority decrement.
 * @param out       message to fill.
 * @return number of entries written.
 */
uint32_t server_stream_priority_build(const uint64_t *ids, const float *box_min,
                                      const float *box_max, uint32_t n,
                                      const float player[3], float scale,
                                      struct net_repl_stream_priority *out);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_SERVER_SERVER_STREAM_PRIORITY_H */
