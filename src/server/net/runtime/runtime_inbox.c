#include <string.h>

#include "runtime_internal.h"

bool fr_server_client_inbox_try_push(fr_server_client_inbox_t *inbox, const uint8_t *packet, size_t size) {
    if (!inbox || (!packet && size != 0u) || size > NET_RUDP_MAX_PACKET_SIZE) {
        return false;
    }

    unsigned w = atomic_load_explicit(&inbox->write_idx, memory_order_relaxed);
    unsigned r = atomic_load_explicit(&inbox->read_idx, memory_order_acquire);
    unsigned next_w = (w + 1u) % FR_SERVER_CLIENT_INBOX_SLOTS;
    if (next_w == (r % FR_SERVER_CLIENT_INBOX_SLOTS)) {
        return false;
    }

    const unsigned slot = w % FR_SERVER_CLIENT_INBOX_SLOTS;
    if (size > 0u) {
        memcpy(inbox->packets[slot], packet, size);
    }
    inbox->sizes[slot] = (uint16_t)size;

    atomic_store_explicit(&inbox->write_idx, w + 1u, memory_order_release);
    return true;
}

bool fr_server_client_inbox_try_pop(fr_server_client_inbox_t *inbox, uint8_t *out_packet, size_t cap, size_t *out_size) {
    if (!inbox || !out_packet || !out_size) {
        return false;
    }

    unsigned r = atomic_load_explicit(&inbox->read_idx, memory_order_relaxed);
    unsigned w = atomic_load_explicit(&inbox->write_idx, memory_order_acquire);
    if (r == w) {
        return false;
    }

    const unsigned slot = r % FR_SERVER_CLIENT_INBOX_SLOTS;
    size_t size = (size_t)inbox->sizes[slot];
    if (size > cap) {
        return false;
    }

    if (size > 0u) {
        memcpy(out_packet, inbox->packets[slot], size);
    }
    *out_size = size;

    atomic_store_explicit(&inbox->read_idx, r + 1u, memory_order_release);
    return true;
}
