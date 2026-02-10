#ifndef FERRUM_NET_TEST_CLIENT_INTERNAL_H
#define FERRUM_NET_TEST_CLIENT_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "ferrum/net/rudp/peer.h"
#include "ferrum/net/stream.h"
#include "ferrum/net/test_link.h"
#include "ferrum/net/topic_channel.h"
#include "ferrum/net/udp_socket.h"

typedef struct fr_test_client {
    net_test_link_t *tx_link;
    net_test_link_t *rx_link;
    net_udp_addr_t remote_addr;

    net_rudp_peer_t peer;
    net_rudp_send_slot_t send_slots[NET_RUDP_SEND_SLOTS_DEFAULT];

    fr_rudp_stream_t *stream;

    fr_topic_channel_t *unreliable_inbox;
} fr_test_client_t;

#endif /* FERRUM_NET_TEST_CLIENT_INTERNAL_H */
