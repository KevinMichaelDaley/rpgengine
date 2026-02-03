#ifndef FERRUM_NET_TOPIC_DISPATCHER_INTERNAL_H
#define FERRUM_NET_TOPIC_DISPATCHER_INTERNAL_H

#include <stdatomic.h>
#include <threads.h>
#include <stdint.h>
#include <stddef.h>

#include "ferrum/net/topic_channel.h"
#include "ferrum/net/topic_dispatcher.h"
#include "ferrum/job/system.h"

typedef struct fr_topic_handler_entry {
    void (*on_message)(const uint8_t *data, size_t len, void *user);
    void *user;
    int priority;
    uint32_t preferred_worker;
} fr_topic_handler_entry_t;

struct fr_topic_dispatcher {
    job_system_t *sys;
    fr_topic_channel_t **topics;
    uint32_t num_topics;
    fr_topic_handler_entry_t *handlers; /* array length = num_topics */
    thrd_t pump_thread;
    atomic_bool running;
};

#endif /* FERRUM_NET_TOPIC_DISPATCHER_INTERNAL_H */
