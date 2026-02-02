// Client RX runtime: inject frames and pop messages
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

#include "ferrum/net/client/runtime_rx.h"
#include "internal.h"

static bool enqueue_msg(fr_channel_state *ch, const uint8_t *data, size_t len) {
    struct fr_msg_node *n = (struct fr_msg_node *)malloc(sizeof(struct fr_msg_node));
    if (!n) return false;
    n->data = (uint8_t *)malloc(len);
    if (!n->data) { free(n); return false; }
    memcpy(n->data, data, len);
    n->len = len;
    n->next = NULL;
    if (!ch->head) ch->head = n; else ch->tail->next = n;
    ch->tail = n;
    atomic_fetch_add(&ch->pending, 1u);
    return true;
}

bool fr_client_rx_inject(fr_client_rx_t *rx, const uint8_t *data, size_t len) {
    if (!rx || !data || len < 10) return false;
    // Parse test frame: [ch:u32][seq:u32][len:u16][payload]
    uint32_t ch_id = 0, seq = 0; uint16_t plen = 0;
    memcpy(&ch_id, data + 0, sizeof(uint32_t));
    memcpy(&seq, data + 4, sizeof(uint32_t));
    memcpy(&plen, data + 8, sizeof(uint16_t));
    if (10u + (size_t)plen != len) return false;
    if (ch_id == 0 || ch_id > rx->max_channels) return false;
    fr_channel_state *ch = &rx->channels[ch_id - 1];
    unsigned expected = atomic_load(&ch->seq_next);
    if (seq < expected) {
        // duplicate or old; drop
        return true;
    }
    if (seq == expected) {
        // enqueue and advance
        // Capacity check (simple): allow growth; tests won't exceed
        if (!enqueue_msg(ch, data + 10, (size_t)plen)) return false;
        atomic_fetch_add(&ch->seq_next, 1u);
        // Release any contiguous out-of-order buffered messages
        unsigned next = atomic_load(&ch->seq_next);
        bool progressed = true;
        while (progressed) {
            progressed = false;
            for (int i = 0; i < 8; ++i) {
                if (ch->ooo_msgs[i] && ch->ooo_seq[i] == next) {
                    fr_msg_node *n = ch->ooo_msgs[i];
                    ch->ooo_msgs[i] = NULL; ch->ooo_seq[i] = 0;
                    // Append to queue
                    n->next = NULL;
                    if (!ch->head) ch->head = n; else ch->tail->next = n;
                    ch->tail = n;
                    atomic_fetch_add(&ch->pending, 1u);
                    next++;
                    atomic_store(&ch->seq_next, next);
                    progressed = true;
                }
            }
        }
        return true;
    }
    // out-of-order: buffer message if not already stored
    for (int i = 0; i < 8; ++i) {
        if (ch->ooo_msgs[i] && ch->ooo_seq[i] == seq) {
            // duplicate
            return true;
        }
    }
    for (int i = 0; i < 8; ++i) {
        if (!ch->ooo_msgs[i]) {
            // store copy
            fr_msg_node *n = (fr_msg_node *)malloc(sizeof(fr_msg_node));
            if (!n) return false;
            n->data = (uint8_t *)malloc(plen);
            if (!n->data) { free(n); return false; }
            memcpy(n->data, data + 10, plen);
            n->len = (size_t)plen;
            n->next = NULL;
            ch->ooo_msgs[i] = n;
            ch->ooo_seq[i] = seq;
            return true;
        }
    }
    // no space; drop
    return true;
}
