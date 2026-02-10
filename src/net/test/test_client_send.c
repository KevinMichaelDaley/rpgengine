#include <stddef.h>
#include <stdint.h>

#include "ferrum/net/test_client.h"
#include "ferrum/net/test_link.h"
#include "ferrum/net/rudp/peer.h"

#include "test_client_internal.h"

static int sendto_link_(void *io_user, const net_udp_addr_t *to, const void *data, size_t size) {
    (void)to;
    net_test_link_t *link = (net_test_link_t *)io_user;
    if (!link || !data) {
        return -1;
    }
    return (net_test_link_send(link, data, size) == NET_TEST_LINK_OK) ? 0 : -1;
}

bool fr_test_client_send_reliable(fr_test_client_t *cl,
                                 uint64_t now_ms,
                                 uint16_t schema_id,
                                 const void *payload,
                                 size_t payload_size) {
    if (!cl || !cl->tx_link || !payload) {
        return false;
    }

    uint16_t seq = 0u;
    return net_rudp_peer_send_reliable_via(&cl->peer,
                                          cl->tx_link,
                                          sendto_link_,
                                          &cl->remote_addr,
                                          now_ms,
                                          schema_id,
                                          payload,
                                          payload_size,
                                          &seq) == NET_RUDP_OK;
}

bool fr_test_client_send_unreliable(fr_test_client_t *cl,
                                   uint64_t now_ms,
                                   uint16_t schema_id,
                                   const void *payload,
                                   size_t payload_size) {
    if (!cl || !cl->tx_link || !payload) {
        return false;
    }

    return net_rudp_peer_send_unreliable_via(&cl->peer,
                                            cl->tx_link,
                                            sendto_link_,
                                            &cl->remote_addr,
                                            now_ms,
                                            schema_id,
                                            payload,
                                            payload_size) == NET_RUDP_OK;
}

bool fr_test_client_tick_resend(fr_test_client_t *cl, uint64_t now_ms) {
    if (!cl || !cl->tx_link) {
        return false;
    }

    return net_rudp_peer_tick_resend_via(&cl->peer,
                                         cl->tx_link,
                                         sendto_link_,
                                         &cl->remote_addr,
                                         now_ms) == NET_RUDP_OK;
}
