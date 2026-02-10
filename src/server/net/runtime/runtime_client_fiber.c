#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/net/packet_header.h"
#include "ferrum/net/stream.h"
#include "ferrum/net/replication/common.h"
#include "ferrum/server/net/inbound_message.h"

#include "runtime_internal.h"

#define OUT_MSG_MAX (2u + NET_RUDP_MAX_PACKET_SIZE)

/**
 * Read the first 4 bytes of a packet as a big-endian uint32.
 * Returns 0 if the packet is too short.
 */
static uint32_t read_protocol_id_(const uint8_t *packet, size_t size) {
    if (size < 4u) {
        return 0u;
    }
    return ((uint32_t)packet[0] << 24u)
         | ((uint32_t)packet[1] << 16u)
         | ((uint32_t)packet[2] << 8u)
         | ((uint32_t)packet[3]);
}

/** Context passed to stream flush sendto callback. */
typedef struct flush_ctx_ {
    fr_server_net_runtime_t *rt;
    net_rudp_peer_t *peer;
    uint16_t client_id;
    uint64_t now_ms;
    FILE *log;
} flush_ctx_t;

static int sendto_(fr_server_net_runtime_t *rt, const net_udp_addr_t *to, const void *data, size_t size) {
    if (!rt || !to || !data) {
        return NET_UDP_SOCKET_ERR_INVALID;
    }
    if (rt->cfg.sendto_cb) {
        return rt->cfg.sendto_cb(rt->cfg.io_user, to, data, size);
    }
    return net_udp_socket_sendto(rt->cfg.socket, to, data, size);
}

static int sendto_cb_bridge_(void *io_user, const net_udp_addr_t *to, const void *data, size_t size) {
    fr_server_net_runtime_t *rt = (fr_server_net_runtime_t *)io_user;
    if (!rt || !to || !data) {
        return -1;
    }
    return (sendto_(rt, to, data, size) == NET_UDP_SOCKET_OK) ? 0 : -1;
}

static void publish_inbound_(fr_server_net_runtime_t *rt,
                             uint16_t client_id,
                             uint8_t reliable,
                             uint16_t schema_id,
                             const uint8_t *payload,
                             size_t payload_size) {
    uint8_t msg[6u + NET_RUDP_MAX_PACKET_SIZE];
    size_t msg_size = 0u;

    if (!rt || !rt->cfg.inbound_topic || payload_size > NET_RUDP_MAX_PACKET_SIZE) {
        return;
    }

    if (!fr_server_net_inbound_message_encode(client_id,
                                              reliable ? true : false,
                                              schema_id,
                                              payload,
                                              payload_size,
                                              msg,
                                              sizeof(msg),
                                              &msg_size)) {
        return;
    }

    (void)fr_topic_channel_push(rt->cfg.inbound_topic, msg, msg_size);
}

/**
 * Stream flush callback: wraps each stream frame as a reliable RUDP
 * packet with schema STREAM_FRAME so the client can demux it.
 */
static int stream_sendto_(void *user, const uint8_t *data, size_t len) {
    flush_ctx_t *ctx = (flush_ctx_t *)user;
    if (!ctx || !ctx->rt || !ctx->peer) {
        return -1;
    }
    /* Log: [seq_lo][seq_hi][chan_lo][chan_hi][payload...] */
    if (ctx->log && len >= 4u) {
        uint16_t seq = (uint16_t)(data[0] | ((uint16_t)data[1] << 8u));
        uint16_t chan = (uint16_t)(data[2] | ((uint16_t)data[3] << 8u));
        fprintf(ctx->log, "SEND seq=%u chan=%u payload_len=%zu\n",
                (unsigned)seq, (unsigned)chan, len - 4u);
        fflush(ctx->log);
    }

    uint16_t rudp_sequence = 0u;
    int rc = net_rudp_peer_send_reliable_via(ctx->peer,
                                             ctx->rt,
                                             sendto_cb_bridge_,
                                             &ctx->rt->clients[ctx->client_id].addr,
                                             ctx->now_ms,
                                             NET_REPL_SCHEMA_STREAM_FRAME,
                                             data, len,
                                             &rudp_sequence);
    (void)rudp_sequence;

    if (rc == NET_RUDP_OK) {
        atomic_fetch_add_explicit(&ctx->rt->packets_out, 1u, memory_order_relaxed);
        atomic_fetch_add_explicit(&ctx->rt->bytes_out, (uint64_t)len, memory_order_relaxed);
    }
    return (rc == NET_RUDP_OK) ? 0 : -1;
}

/**
 * Drain the reliable outbound topic into the stream's outbound channel.
 * Each topic message is [schema_id:u16 LE][payload].  We queue the entire
 * topic message (including schema_id prefix) as the stream payload so the
 * receiver can decode schema + payload after reassembly.
 */
static void pump_reliable_to_stream_(fr_rudp_stream_t *stream,
                                     fr_topic_channel_t *topic) {
    uint8_t msg[OUT_MSG_MAX];
    for (;;) {
        size_t len = sizeof(msg);
        if (!fr_topic_channel_pop(topic, msg, &len)) {
            break;
        }
        if (len < 2u) {
            continue;
        }
        /* Queue the full message (schema_id + payload) into stream channel 0. */
        if (!fr_rudp_stream_send(stream, 0u, msg, len)) {
            /* Stream channel full -- push back and retry next tick. */
            (void)fr_topic_channel_push(topic, msg, len);
            break;
        }
    }
}

/**
 * Drain the unreliable outbound topic and send directly via the RUDP peer.
 */
static void pump_unreliable_topic_(fr_server_net_runtime_t *rt,
                                   uint16_t client_id,
                                   fr_topic_channel_t *topic,
                                   net_rudp_peer_t *peer,
                                   uint64_t now_ms) {
    uint8_t msg[OUT_MSG_MAX];
    for (;;) {
        size_t len = sizeof(msg);
        if (!fr_topic_channel_pop(topic, msg, &len)) {
            break;
        }
        if (len < 2u) {
            continue;
        }
        uint16_t schema_id = (uint16_t)msg[0] | ((uint16_t)msg[1] << 8u);
        const uint8_t *payload = msg + 2u;
        size_t payload_size = len - 2u;

        int rc = net_rudp_peer_send_unreliable_via(peer,
                                                   rt,
                                                   sendto_cb_bridge_,
                                                   &rt->clients[client_id].addr,
                                                   now_ms,
                                                   schema_id,
                                                   payload,
                                                   payload_size);
        if (rc == NET_RUDP_OK) {
            atomic_fetch_add_explicit(&rt->packets_out, 1u, memory_order_relaxed);
            atomic_fetch_add_explicit(&rt->bytes_out, (uint64_t)payload_size, memory_order_relaxed);
        }
    }
}

void fr_server_client_fiber_main(void *user) {
    fr_server_client_fiber_args_t *args = (fr_server_client_fiber_args_t *)user;
    if (!args || !args->rt) {
        free(args);
        return;
    }
    fr_server_net_runtime_t *rt = args->rt;
    const uint16_t client_id = args->client_id;
    free(args);

    if (client_id >= rt->cfg.max_clients) {
        return;
    }
    fr_server_client_t *client = &rt->clients[client_id];

    fr_server_client_inbox_t inbox;
    memset(&inbox, 0, sizeof(inbox));
    atomic_init(&inbox.read_idx, 0u);
    atomic_init(&inbox.write_idx, 0u);
    atomic_store_explicit(&client->inbox_ptr, (uintptr_t)&inbox, memory_order_release);

    if (atomic_load_explicit(&client->pending_used, memory_order_acquire)) {
        size_t staged = (size_t)client->pending_size;
        if (staged > 0u && staged <= NET_RUDP_MAX_PACKET_SIZE) {
            (void)fr_server_client_inbox_try_push(&inbox, client->pending_packet, staged);
        }
        atomic_store_explicit(&client->pending_used, false, memory_order_release);
        client->pending_size = 0u;
    }

    /* RUDP peer for inbound decoding and unreliable outbound. */
    net_rudp_peer_t peer;
    net_rudp_peer_init_with_storage(&peer, NET_RUDP_PROTOCOL_ID_P008, 50u,
                                    client->send_slots, client->send_slot_count);

    /* Stream for reliable outbound: messages are sequenced, framed, and
     * sent as unreliable RUDP packets with STREAM_FRAME schema.  The
     * client's stream layer reassembles them in order. */
    fr_rudp_stream_config_t scfg;
    memset(&scfg, 0, sizeof(scfg));
    scfg.reliable_channels = 1u;
    scfg.reliable_slot_count = 512u;  /* large window for spawn bursts */
    scfg.max_payload_size = NET_RUDP_MAX_PACKET_SIZE;
    fr_rudp_stream_t *out_stream = fr_rudp_stream_create(&scfg);
    if (!out_stream) {
        return;
    }

    /* Diagnostic: log every stream frame sent. */
    FILE *stream_log = fopen("/tmp/stream_server_send.log", "w");

    uint8_t packet[NET_RUDP_MAX_PACKET_SIZE];
    uint8_t payload[NET_RUDP_MAX_PACKET_SIZE];

    for (;;) {
        if (atomic_load_explicit(&client->stop, memory_order_acquire)) {
            break;
        }
        if (atomic_load_explicit(&rt->cfg.jobs->shutting_down, memory_order_acquire)) {
            break;
        }

        uint64_t now_ms = atomic_load_explicit(&client->now_ms, memory_order_acquire);

        /* Inbound packets: demux RUDP vs raw UDP datagrams.
         * RUDP packets start with the protocol_id (big-endian).
         * Raw datagrams start with [schema_id:u16 LE][payload]. */
        for (;;) {
            size_t packet_size = 0u;
            if (!fr_server_client_inbox_try_pop(&inbox, packet, sizeof(packet), &packet_size)) {
                break;
            }

            const uint32_t proto = read_protocol_id_(packet, packet_size);
            if (proto == NET_RUDP_PROTOCOL_ID_P008) {
                /* RUDP path: feed through peer for ack/reliability handling. */
                uint8_t reliable = 0u;
                uint16_t schema_id = 0u;
                size_t payload_size = 0u;
                int prc = net_rudp_peer_receive(&peer,
                                                packet,
                                                packet_size,
                                                now_ms,
                                                &reliable,
                                                &schema_id,
                                                payload,
                                                sizeof(payload),
                                                &payload_size);
                if (prc != NET_RUDP_OK) {
                    continue;
                }
                publish_inbound_(rt, client_id, reliable, schema_id, payload, payload_size);
            } else if (packet_size >= 2u) {
                /* Raw unreliable datagram: [schema_id:u16 LE][payload]. */
                uint16_t schema_id = (uint16_t)packet[0] | ((uint16_t)packet[1] << 8u);
                const uint8_t *raw_payload = packet + 2u;
                size_t raw_payload_size = packet_size - 2u;
                publish_inbound_(rt, client_id, 0u, schema_id, raw_payload, raw_payload_size);
            }
            /* Packets < 2 bytes are silently dropped. */
        }

        /* Reliable outbound: drain topic -> stream -> flush as RUDP frames */
        pump_reliable_to_stream_(out_stream, client->out_reliable);

        flush_ctx_t fctx = { .rt = rt, .peer = &peer,
                             .client_id = client_id, .now_ms = now_ms,
                             .log = stream_log };
        (void)fr_rudp_stream_flush_send(out_stream, stream_sendto_, &fctx);

        /* Unreliable outbound: send directly via RUDP peer */
        pump_unreliable_topic_(rt, client_id, client->out_unreliable, &peer, now_ms);

        /* Reliable resend for any peer-level reliable traffic */
        (void)net_rudp_peer_tick_resend_via(&peer, rt, sendto_cb_bridge_, &client->addr, now_ms);

        job_yield();
    }

    if (stream_log) fclose(stream_log);
    fr_rudp_stream_destroy(out_stream);
}
