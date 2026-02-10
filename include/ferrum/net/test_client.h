/**
 * @file
 * @brief Headless deterministic test client for in-process networking integration tests.
 *
 * This module is intended for CI/headless tests. It uses:
 * - net_test_link_t for deterministic delivery (loss/dup/reorder/jitter)
 * - net_rudp_peer_t for reliable UDP framing + resend
 * - fr_rudp_stream_t for ordered reliable message reassembly (STREAM_FRAME payloads)
 *
 * Ownership: caller owns the returned client context and must destroy it.
 * Threading: single-threaded; caller drives pump functions.
 */
#ifndef FERRUM_NET_TEST_CLIENT_H
#define FERRUM_NET_TEST_CLIENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ferrum/net/test_link.h"
#include "ferrum/net/udp_socket.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Configuration for the headless test client. */
typedef struct fr_test_client_config_t {
    /** RUDP protocol id (e.g. NET_RUDP_PROTOCOL_ID_P008). */
    uint32_t protocol_id;

    /** Resend interval (ms) for reliable packets. 0 uses a default. */
    uint32_t resend_interval_ms;

    /** Deterministic link for TX (client -> server). Non-NULL. */
    net_test_link_t *tx_link;

    /** Deterministic link for RX (server -> client). Non-NULL. */
    net_test_link_t *rx_link;

    /** Address passed to RUDP send functions (send callback may ignore). */
    net_udp_addr_t remote_addr;

    /** Reliable stream channel count for STREAM_FRAME reassembly. 0 defaults to 1. */
    uint32_t stream_channels;

    /** Reliable stream slot/window size. 0 defaults to 64. */
    uint32_t stream_slots;

    /** Max payload size for reliable stream messages. 0 defaults to NET_RUDP_MAX_PACKET_SIZE. */
    uint32_t stream_max_payload;

    /** Capacity of unreliable inbox (messages). 0 defaults to 64. */
    uint32_t unreliable_inbox_capacity;
} fr_test_client_config_t;

/** Opaque client context. */
typedef struct fr_test_client fr_test_client_t;

/** Create a headless test client. Returns NULL on invalid args or allocation failure. */
fr_test_client_t *fr_test_client_create(const fr_test_client_config_t *cfg);

/** Destroy a headless test client. Safe to call with NULL. */
void fr_test_client_destroy(fr_test_client_t *cl);

/** Send a reliable RUDP message onto the TX link. Returns false on error/backpressure. */
bool fr_test_client_send_reliable(fr_test_client_t *cl,
                                 uint64_t now_ms,
                                 uint16_t schema_id,
                                 const void *payload,
                                 size_t payload_size);

/** Send an unreliable RUDP message onto the TX link. Returns false on error/backpressure. */
bool fr_test_client_send_unreliable(fr_test_client_t *cl,
                                   uint64_t now_ms,
                                   uint16_t schema_id,
                                   const void *payload,
                                   size_t payload_size);

/** Tick RUDP resend for any unACKed reliable packets. */
bool fr_test_client_tick_resend(fr_test_client_t *cl, uint64_t now_ms);

/** Pump RX link packets into the client's RUDP peer and stream reassembly. */
bool fr_test_client_pump_rx(fr_test_client_t *cl, uint64_t now_ms);

/** Pop the next reassembled reliable stream message for a stream channel (0-based). */
bool fr_test_client_pop_reliable(fr_test_client_t *cl,
                                uint32_t channel_id,
                                uint8_t *out,
                                size_t *inout_len);

/** Pop the next received unreliable message. Output schema_id and payload bytes. */
bool fr_test_client_pop_unreliable(fr_test_client_t *cl,
                                  uint16_t *out_schema_id,
                                  uint8_t *out,
                                  size_t *inout_len);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_NET_TEST_CLIENT_H */
