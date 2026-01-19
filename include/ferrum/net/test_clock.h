#ifndef FERRUM_NET_TEST_CLOCK_H
#define FERRUM_NET_TEST_CLOCK_H

#include <stdint.h>

/** @file
 * @brief Deterministic test clock for networking simulations.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Deterministic test clock storing time in nanoseconds. */
typedef struct net_test_clock {
    uint64_t now_ns;
} net_test_clock_t;

/**
 * @brief Initialize the test clock.
 * @param clock Clock pointer (non-NULL).
 * @param start_ns Initial time in nanoseconds.
 */
void net_test_clock_init(net_test_clock_t *clock, uint64_t start_ns);

/**
 * @brief Read the current time in nanoseconds.
 * @param clock Clock pointer.
 * @return Current time in nanoseconds (0 if clock is NULL).
 */
uint64_t net_test_clock_now_ns(const net_test_clock_t *clock);

/**
 * @brief Advance the clock by a delta.
 * @param clock Clock pointer.
 * @param delta_ns Delta in nanoseconds.
 */
void net_test_clock_advance(net_test_clock_t *clock, uint64_t delta_ns);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_NET_TEST_CLOCK_H */
