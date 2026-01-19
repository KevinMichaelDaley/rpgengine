#include "ferrum/net/test_clock.h"

void net_test_clock_init(net_test_clock_t *clock, uint64_t start_ns) {
    if (!clock) {
        return;
    }
    clock->now_ns = start_ns;
}

uint64_t net_test_clock_now_ns(const net_test_clock_t *clock) {
    if (!clock) {
        return 0u;
    }
    return clock->now_ns;
}

void net_test_clock_advance(net_test_clock_t *clock, uint64_t delta_ns) {
    if (!clock) {
        return;
    }
    clock->now_ns += delta_ns;
}
