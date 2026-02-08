#include <stdlib.h>
#include "ferrum/net/topic_channel.h"
#include "channel_internal.h"

void fr_topic_channel_destroy(fr_topic_channel_t *ch) {
    if (!ch) return;
    job_spinlock_lock(&ch->lock);
    if (ch->ring) free(ch->ring);
    job_spinlock_unlock(&ch->lock);
    job_spinlock_destroy(&ch->lock);
    free(ch);
}
