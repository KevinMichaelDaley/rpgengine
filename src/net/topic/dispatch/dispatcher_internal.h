#ifndef FERRUM_NET_TOPIC_DISPATCHER_INTERNAL_H
#define FERRUM_NET_TOPIC_DISPATCHER_INTERNAL_H

#include <stdatomic.h>
#include <pthread.h>
#include <stdint.h>
#include <stddef.h>

#include "ferrum/net/topic_channel.h"
#include "ferrum/net/topic_dispatcher.h"
#include "ferrum/memory/apool.h"
#include "ferrum/job/system.h"

#define FR_TOPIC_DISPATCHER_MAX_MESSAGE_SIZE 1024u

typedef struct fr_topic_handler_entry {
    void (*on_message)(const uint8_t *data, size_t len, void *user);
    void *user;
    int priority;
    uint32_t preferred_worker;
} fr_topic_handler_entry_t;

typedef struct fr_topic_dispatch_payload {
    fr_topic_handler_entry_t entry;
    uint8_t *data;
    size_t len;
    apool_t *payload_pool;
    apool_handle_t payload_handle;
    apool_t *data_pool;
    apool_handle_t data_handle;
} fr_topic_dispatch_payload_t;

struct fr_topic_dispatcher {
    job_system_t *sys;
    fr_topic_channel_t **topics;
    uint32_t num_topics;
    fr_topic_handler_entry_t *handlers; /* array length = num_topics */

    apool_t payload_pool;
    apool_t data_pool;

    pthread_t pump_thread;
    atomic_bool running;
};

#endif /* FERRUM_NET_TOPIC_DISPATCHER_INTERNAL_H */
