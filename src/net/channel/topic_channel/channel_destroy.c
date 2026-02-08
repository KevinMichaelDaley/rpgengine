#include <stdlib.h>
#include "ferrum/net/topic_channel.h"
#include "channel_internal.h"

void fr_topic_channel_destroy(fr_topic_channel_t *ch) {
    if (!ch) return;
    pthread_mutex_lock(&ch->lock);
    if (ch->ring) free(ch->ring);
    pthread_mutex_unlock(&ch->lock);
    pthread_mutex_destroy(&ch->lock);
    free(ch);
}
