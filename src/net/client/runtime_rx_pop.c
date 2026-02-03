// Client RX runtime: pop ordered messages per channel
#include <string.h>
#include <stdatomic.h>
#include <stdlib.h>

#include "ferrum/net/client/runtime_rx.h"
#include "internal.h"

bool fr_client_rx_pop_message(fr_client_rx_t *rx, uint32_t channel_id, uint8_t *out, size_t *inout_len) {
    if (!rx || !rx->stream || channel_id == 0 || channel_id > rx->max_channels || !out || !inout_len) return false;
    // Stream channels are 0-based
    return fr_rudp_stream_pop(rx->stream, channel_id - 1u, out, inout_len);
}
