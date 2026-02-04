#include "ferrum/net/topic_channel.h"
#include "channel_internal.h"

uint64_t fr_topic_channel_stat_dropped(const fr_topic_channel_t *ch) {
    if (!ch) return 0u;
    return (uint64_t)atomic_load(&ch->stat_dropped);
}
