// Client RX runtime: inject frames and pop messages
#include <string.h>
#include <stdatomic.h>

#include "ferrum/net/client/runtime_rx.h"
#include "internal.h"

bool fr_client_rx_inject(fr_client_rx_t *rx, const uint8_t *data, size_t len) {
    if (!rx || !rx->stream || !rx->frame_buf || !data || len < 10) return false;
    /* Parse test frame: [ch:u32][seq:u32][len:u16][payload] */
    uint32_t ch_id = 0, seq32 = 0; uint16_t plen = 0;
    memcpy(&ch_id, data + 0, sizeof(uint32_t));
    memcpy(&seq32, data + 4, sizeof(uint32_t));
    memcpy(&plen, data + 8, sizeof(uint16_t));
    if (10u + (size_t)plen != len) return false;
    if (ch_id == 0 || ch_id > rx->max_channels) return false;
    /* Bounds-check against pre-allocated frame buffer. */
    size_t frame_len = 4u + (size_t)plen;
    if (frame_len > RX_FRAME_BUF_SIZE) return false;
    uint16_t seq = (uint16_t)(seq32 & 0xFFFFu);
    uint16_t chan = (uint16_t)(ch_id - 1u);
    /* Build stream frame in pre-allocated buffer — no per-packet allocation. */
    uint8_t *frame = rx->frame_buf;
    frame[0] = (uint8_t)(seq & 0xFFu);
    frame[1] = (uint8_t)((seq >> 8) & 0xFFu);
    frame[2] = (uint8_t)(chan & 0xFFu);
    frame[3] = (uint8_t)((chan >> 8) & 0xFFu);
    memcpy(frame + 4u, data + 10u, (size_t)plen);
    (void)fr_rudp_stream_push_frame(rx->stream, frame, frame_len);
    return true;
}
