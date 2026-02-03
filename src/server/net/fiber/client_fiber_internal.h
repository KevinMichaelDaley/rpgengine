#ifndef CLIENT_FIBER_INTERNAL_H
#define CLIENT_FIBER_INTERNAL_H

#include <stddef.h>
#include "ferrum/net/stream.h"
#include "ferrum/server/net/client_fiber.h"

typedef struct fr_server_client_fiber_t {
    fr_rudp_stream_t *stream;
    fr_topic_channel_t **topics;
    unsigned num_topics;
} fr_server_client_fiber_t;

#endif
