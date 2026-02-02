// Client RX runtime: pop ordered messages per channel
#include <string.h>
#include <stdatomic.h>
#include <stdlib.h>

#include "ferrum/net/client/runtime_rx.h"
#include "internal.h"

bool fr_client_rx_pop_message(fr_client_rx_t *rx, uint32_t channel_id, uint8_t *out, size_t *inout_len) {
    if (!rx || channel_id == 0 || channel_id > rx->max_channels || !out || !inout_len) return false;
    fr_channel_state *ch = &rx->channels[channel_id - 1];
    struct fr_msg_node *n = ch->head;
    if (!n) return false;
    size_t cap = *inout_len;
    if (n->len > cap) return false;
    memcpy(out, n->data, n->len);
    *inout_len = n->len;
    ch->head = n->next;
    if (!ch->head) ch->tail = NULL;
    free(n->data);
    free(n);
    atomic_fetch_sub(&ch->pending, 1u);
    return true;
}
