#include <stdlib.h>
#include <string.h>
#include "dispatcher_internal.h"

fr_topic_dispatcher_t *fr_topic_dispatcher_create(job_system_t *sys,
                                                  fr_topic_channel_t **topics,
                                                  uint32_t num_topics) {
    if (!sys || !topics || num_topics == 0) return NULL;
    fr_topic_dispatcher_t *d = (fr_topic_dispatcher_t*)calloc(1, sizeof(fr_topic_dispatcher_t));
    if (!d) return NULL;
    d->sys = sys;
    d->num_topics = num_topics;
    d->topics = (fr_topic_channel_t**)calloc(num_topics, sizeof(fr_topic_channel_t*));
    if (!d->topics) { free(d); return NULL; }
    for (uint32_t i=0;i<num_topics;i++) d->topics[i] = topics[i];
    d->handlers = (fr_topic_handler_entry_t*)calloc(num_topics, sizeof(fr_topic_handler_entry_t));
    if (!d->handlers) { free(d->topics); free(d); return NULL; }

    uint32_t pool_capacity = num_topics * 64u;
    if (pool_capacity < 128u) pool_capacity = 128u;
    if (pool_capacity > 8192u) pool_capacity = 8192u;

    if (apool_init(&d->payload_pool, pool_capacity, (uint32_t)sizeof(fr_topic_dispatch_payload_t)) != APOOL_OK) {
        free(d->handlers);
        free(d->topics);
        free(d);
        return NULL;
    }
    if (apool_init(&d->data_pool, pool_capacity, FR_TOPIC_DISPATCHER_MAX_MESSAGE_SIZE) != APOOL_OK) {
        apool_destroy(&d->payload_pool);
        free(d->handlers);
        free(d->topics);
        free(d);
        return NULL;
    }

    atomic_store(&d->running, false);
    return d;
}

void fr_topic_dispatcher_destroy(fr_topic_dispatcher_t *disp) {
    if (!disp) return;
    apool_destroy(&disp->data_pool);
    apool_destroy(&disp->payload_pool);
    free(disp->handlers);
    free(disp->topics);
    free(disp);
}
