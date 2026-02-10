/**
 * @file jitter_buffer.c
 * @brief Jitter buffer: tracks arrival variance, produces margin.
 *
 * Non-static functions: 3 (init, sample, margin).
 */

#include "ferrum/net/time_sync.h"
#include <string.h>

void net_jitter_buffer_init(net_jitter_buffer_t *jbuf,
                            uint32_t window_size) {
    if (!jbuf) { return; }
    memset(jbuf, 0, sizeof(*jbuf));
    if (window_size > NET_JITTER_BUF_MAX_WINDOW) {
        window_size = NET_JITTER_BUF_MAX_WINDOW;
    }
    if (window_size == 0) { window_size = 1; }
    jbuf->window_size = window_size;
}

void net_jitter_buffer_sample(net_jitter_buffer_t *jbuf,
                              uint64_t expected_ms,
                              uint64_t actual_ms) {
    if (!jbuf) { return; }

    /* Absolute jitter = |actual - expected|. */
    uint64_t jitter;
    if (actual_ms >= expected_ms) {
        jitter = actual_ms - expected_ms;
    } else {
        jitter = expected_ms - actual_ms;
    }

    jbuf->jitter_samples[jbuf->write_idx] = jitter;
    jbuf->write_idx = (jbuf->write_idx + 1) % jbuf->window_size;
    if (jbuf->sample_count < jbuf->window_size) {
        jbuf->sample_count++;
    }
}

uint64_t net_jitter_buffer_margin(const net_jitter_buffer_t *jbuf) {
    if (!jbuf || jbuf->sample_count == 0) { return 0; }

    /* Return the max jitter in the window as the safety margin. */
    uint64_t max_jitter = 0;
    for (uint32_t i = 0; i < jbuf->sample_count; i++) {
        if (jbuf->jitter_samples[i] > max_jitter) {
            max_jitter = jbuf->jitter_samples[i];
        }
    }
    return max_jitter;
}
