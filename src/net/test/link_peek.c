#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ferrum/net/test_link.h"

bool net_test_link_next_delivery_time_ns(const net_test_link_t *link, uint64_t *out_time_ns) {
    if (!link || !out_time_ns) {
        return false;
    }

    int have_best = 0;
    uint64_t best = 0u;
    for (size_t i = 0u; i < link->slot_count; ++i) {
        if (!link->slot_used[i]) {
            continue;
        }
        const uint64_t t = link->deliver_times[i];
        if (!have_best || t < best) {
            have_best = 1;
            best = t;
        }
    }

    if (!have_best) {
        return false;
    }

    *out_time_ns = best;
    return true;
}
