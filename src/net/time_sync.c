/**
 * @file time_sync.c
 * @brief Time synchronization offset estimator with median filter + drift clamp.
 *
 * Non-static functions: 3 (init, sample, offset).
 */

#include "ferrum/net/time_sync.h"
#include <string.h>

void net_time_sync_init(net_time_sync_t *sync,
                        uint32_t window_size,
                        int64_t max_drift_per_update) {
    if (!sync) { return; }
    memset(sync, 0, sizeof(*sync));
    if (window_size > NET_TIME_SYNC_MAX_WINDOW) {
        window_size = NET_TIME_SYNC_MAX_WINDOW;
    }
    if (window_size == 0) { window_size = 1; }
    sync->window_size = window_size;
    sync->max_drift_per_update = max_drift_per_update;
}

/**
 * Compute median of up to `n` int64 values.  Uses a scratch copy
 * and insertion sort (window is small, ≤32).
 */
static int64_t compute_median(const int64_t *values, uint32_t n) {
    if (n == 0) { return 0; }
    if (n == 1) { return values[0]; }

    /* Copy into scratch and insertion-sort. */
    int64_t sorted[NET_TIME_SYNC_MAX_WINDOW];
    for (uint32_t i = 0; i < n; i++) { sorted[i] = values[i]; }

    for (uint32_t i = 1; i < n; i++) {
        int64_t key = sorted[i];
        uint32_t j = i;
        while (j > 0 && sorted[j - 1] > key) {
            sorted[j] = sorted[j - 1];
            j--;
        }
        sorted[j] = key;
    }

    return sorted[n / 2];
}

void net_time_sync_sample(net_time_sync_t *sync,
                          uint64_t server_time_ms,
                          uint64_t client_time_ms) {
    if (!sync) { return; }

    int64_t raw_offset = (int64_t)server_time_ms - (int64_t)client_time_ms;

    /* Store in ring buffer. */
    sync->samples[sync->write_idx] = raw_offset;
    sync->write_idx = (sync->write_idx + 1) % sync->window_size;
    if (sync->sample_count < sync->window_size) {
        sync->sample_count++;
    }

    /* Compute median of current window. */
    int64_t median = compute_median(sync->samples, sync->sample_count);

    if (!sync->initialized) {
        /* First sample: snap directly. */
        sync->applied_offset = median;
        sync->initialized = 1;
        return;
    }

    /* Drift clamp: move applied_offset toward median by at most
     * max_drift_per_update. */
    int64_t diff = median - sync->applied_offset;
    if (diff > sync->max_drift_per_update) {
        sync->applied_offset += sync->max_drift_per_update;
    } else if (diff < -sync->max_drift_per_update) {
        sync->applied_offset -= sync->max_drift_per_update;
    } else {
        sync->applied_offset = median;
    }
}

int64_t net_time_sync_offset(const net_time_sync_t *sync) {
    if (!sync) { return 0; }
    return sync->applied_offset;
}
