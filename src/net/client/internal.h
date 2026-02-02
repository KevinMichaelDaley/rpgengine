#ifndef FERRUM_NET_CLIENT_INTERNAL_H
#define FERRUM_NET_CLIENT_INTERNAL_H

#include <stddef.h>
#include <stdint.h>
#include <pthread.h>
#include <stdatomic.h>

typedef struct fr_msg_node {
    uint8_t *data;
    size_t len;
    struct fr_msg_node *next;
} fr_msg_node;

typedef struct fr_channel_state {
    atomic_uint seq_next; // next expected seq (starting at 1)
    fr_msg_node *head;
    fr_msg_node *tail;
    atomic_uint pending; // buffered messages
    fr_msg_node *ooo_msgs[8];
    unsigned ooo_seq[8];
} fr_channel_state;

typedef struct fr_client_rx_t {
    uint32_t max_channels;
    uint32_t max_pending;
    fr_channel_state *channels;
    pthread_t thread;
    atomic_bool running;
    ssize_t (*recv_cb)(void *user, uint8_t *buf, size_t cap);
    void *recv_user;
} fr_client_rx_t;

#endif // FERRUM_NET_CLIENT_INTERNAL_H
