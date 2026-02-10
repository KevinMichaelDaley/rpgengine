#ifndef FERRUM_NET_TEST_LINK_H
#define FERRUM_NET_TEST_LINK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ferrum/net/test_clock.h"

/** @file
 * @brief Deterministic packet simulation link for network tests.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Status: operation succeeded. */
#define NET_TEST_LINK_OK 0
/** Status: no packet ready for delivery. */
#define NET_TEST_LINK_EMPTY 1
/** Status: invalid arguments. */
#define NET_TEST_LINK_ERR_INVALID -1
/** Status: allocation failed. */
#define NET_TEST_LINK_ERR_OOM -2
/** Status: delivery queue full. */
#define NET_TEST_LINK_ERR_FULL -3

/** Scripted send step for deterministic loss/dup/jitter. */
typedef struct net_test_step {
    uint32_t copies;
    uint64_t delay_ns;
    uint64_t duplicate_delay_ns;
} net_test_step_t;

/** Deterministic packet link with scripted behavior. */
typedef struct net_test_link {
    net_test_clock_t *clock;
    const net_test_step_t *steps;
    size_t step_count;
    size_t step_index;
    uint8_t *payload_storage;
    uint16_t *payload_sizes;
    uint64_t *deliver_times;
    uint8_t *slot_used;
    size_t slot_count;
    size_t payload_stride;
} net_test_link_t;

/**
 * @brief Initialize a test link with scripted steps.
 * @param link Link pointer (non-NULL).
 * @param clock Clock pointer (non-NULL).
 * @param steps Script steps array (may be NULL if step_count == 0).
 * @param step_count Number of script steps.
 * @param slot_count Maximum packets that can be queued for delivery.
 * @param max_payload_size Maximum payload size in bytes.
 * @return NET_TEST_LINK_OK on success or error code.
 */
int net_test_link_init(net_test_link_t *link,
                       net_test_clock_t *clock,
                       const net_test_step_t *steps,
                       size_t step_count,
                       size_t slot_count,
                       size_t max_payload_size);

/**
 * @brief Destroy a test link and free internal storage.
 * @param link Link pointer (NULL-safe).
 */
void net_test_link_destroy(net_test_link_t *link);

/**
 * @brief Enqueue a payload for delivery according to the scripted step.
 * @param link Link pointer.
 * @param payload Payload bytes to copy.
 * @param payload_size Payload size in bytes.
 * @return NET_TEST_LINK_OK on success or error code.
 */
int net_test_link_send(net_test_link_t *link, const void *payload, size_t payload_size);

/**
 * @brief Receive the next ready payload if its delivery time has arrived.
 * @param link Link pointer.
 * @param out_payload Output buffer for payload bytes.
 * @param out_capacity Output buffer capacity in bytes.
 * @param out_size Output payload size.
 * @return NET_TEST_LINK_OK on success, NET_TEST_LINK_EMPTY if none ready, or error code.
 */
int net_test_link_receive(net_test_link_t *link,
                          void *out_payload,
                          size_t out_capacity,
                          size_t *out_size);

/**
 * @brief Query the earliest queued delivery time for the link.
 * @param link Link pointer.
 * @param out_time_ns Output earliest delivery timestamp in nanoseconds.
 * @return true if a packet is queued, false if none or on invalid args.
 */
bool net_test_link_next_delivery_time_ns(const net_test_link_t *link, uint64_t *out_time_ns);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_NET_TEST_LINK_H */
