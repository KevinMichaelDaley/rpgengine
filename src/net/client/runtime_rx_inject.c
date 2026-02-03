// Client RX runtime: inject frames and pop messages
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

#include "ferrum/net/client/runtime_rx.h"
#include "internal.h"

// Legacy enqueue removed; stream handles reassembly and topic pumping.

bool fr_client_rx_inject(fr_client_rx_t *rx, const uint8_t *data, size_t len) {
    if (!rx || !rx->stream || !data || len < 10) return false;
    // Parse test frame: [ch:u32][seq:u32][len:u16][payload]
    uint32_t ch_id = 0, seq32 = 0; uint16_t plen = 0;
    memcpy(&ch_id, data + 0, sizeof(uint32_t));
    memcpy(&seq32, data + 4, sizeof(uint32_t));
    memcpy(&plen, data + 8, sizeof(uint16_t));
    if (10u + (size_t)plen != len) return false;
    if (ch_id == 0 || ch_id > rx->max_channels) return false;
    uint16_t seq = (uint16_t)(seq32 & 0xFFFFu);
    uint16_t chan = (uint16_t)(ch_id - 1u);
    // Build stream frame: [seq:u16 LE][chan:u16 LE][payload]
    size_t frame_len = 4u + (size_t)plen;
    uint8_t *frame = (uint8_t *)malloc(frame_len);
    if (!frame) return false;
    frame[0] = (uint8_t)(seq & 0xFFu);
    frame[1] = (uint8_t)((seq >> 8) & 0xFFu);
    frame[2] = (uint8_t)(chan & 0xFFu);
    frame[3] = (uint8_t)((chan >> 8) & 0xFFu);
    memcpy(frame + 4u, data + 10u, (size_t)plen);
    (void)fr_rudp_stream_push_frame(rx->stream, frame, frame_len);
    free(frame);
    // Treat duplicates/out-of-window frames as accepted no-ops
    return true;
}
