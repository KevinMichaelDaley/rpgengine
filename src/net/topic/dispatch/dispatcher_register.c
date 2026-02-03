#include "dispatcher_internal.h"

int fr_topic_dispatcher_register(fr_topic_dispatcher_t *disp,
                                 uint32_t topic_index,
                                 void (*on_message)(const uint8_t *data, size_t len, void *user),
                                 void *user,
                                 int priority,
                                 uint32_t preferred_worker) {
    if (!disp || topic_index >= disp->num_topics || !on_message) return -1;
    disp->handlers[topic_index].on_message = on_message;
    disp->handlers[topic_index].user = user;
    disp->handlers[topic_index].priority = priority;
    disp->handlers[topic_index].preferred_worker = preferred_worker;
    return 0;
}
