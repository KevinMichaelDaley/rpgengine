/**
 * @file demo_server.c
 * @brief Headless demo server binary.
 *
 * Creates a UDP socket, listens for up to 4 RUDP clients, processes
 * INPUT_MOVE and INPUT_SPAWN messages, ticks a physics world at a
 * fixed rate, and broadcasts body state (STATE_CUBE) to all clients.
 *
 * Usage: ./build/demo_server <port> [tick_hz] [workers]
 *   Default: port=40080 tick_hz=60 workers=4
 */

#define _POSIX_C_SOURCE 200809L

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <errno.h>
#include <signal.h>
#include <sched.h>
#include <time.h>

#include "ferrum/job/system.h"
#include "ferrum/net/rudp/peer.h"
#include "ferrum/net/quantization.h"
#include "ferrum/net/replication/common.h"
#include "ferrum/net/replication/state_cube.h"
#include "ferrum/net/udp_socket.h"
#include "ferrum/net/topic_channel.h"

#include "ferrum/server/entity/net/pump.h"
#include "ferrum/server/net/runtime.h"

#include "ferrum/demo/server_world.h"
#include "ferrum/demo/input_move.h"
#include "ferrum/demo/input_spawn.h"

/* ── Time helpers ───────────────────────────────────────────────── */

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)ts.tv_nsec / 1000000ull;
}

static void sleep_ms(uint32_t ms) {
    struct timespec ts;
    ts.tv_sec  = (time_t)(ms / 1000u);
    ts.tv_nsec = (long)((ms % 1000u) * 1000u * 1000u);
    nanosleep(&ts, NULL);
}

/* ── Signal handling ────────────────────────────────────────────── */

static volatile sig_atomic_t g_stop = 0;

static void handle_stop_signal(int signum) {
    (void)signum;
    g_stop = 1;
}

/* ── I/O counting wrappers ──────────────────────────────────────── */

struct demo_server_io {
    net_udp_socket_t *sock;
    uint64_t packets_sent;
    uint64_t packets_recv;
    uint64_t bytes_sent;
    uint64_t bytes_recv;
};

static int recvfrom_counting(void *user,
                             net_udp_addr_t *out_from,
                             uint8_t *out_data,
                             size_t out_cap,
                             size_t *out_size) {
    struct demo_server_io *io = (struct demo_server_io *)user;
    if (!io || !io->sock) {
        return NET_UDP_SOCKET_ERR_INVALID;
    }
    int rc = net_udp_socket_recvfrom(io->sock, out_from, out_data, out_cap, out_size);
    if (rc == NET_UDP_SOCKET_OK && out_size) {
        io->packets_recv += 1u;
        io->bytes_recv += (uint64_t)(*out_size);
    }
    return rc;
}

static int sendto_counting(void *user, const net_udp_addr_t *to,
                           const void *data, size_t size) {
    struct demo_server_io *io = (struct demo_server_io *)user;
    if (!io || !io->sock) {
        return NET_UDP_SOCKET_ERR_INVALID;
    }
    int rc = net_udp_socket_sendto(io->sock, to, data, size);
    if (rc == NET_UDP_SOCKET_OK) {
        io->packets_sent += 1u;
        io->bytes_sent += (uint64_t)size;
    }
    return rc;
}

/* ── Pump callback: resolve per-client outbound topics ──────────── */

static bool get_out_topics_from_runtime(void *user,
                                        uint16_t client_id,
                                        fr_topic_channel_t **out_rel,
                                        fr_topic_channel_t **out_unrel) {
    fr_server_net_runtime_t *rt = (fr_server_net_runtime_t *)user;
    if (!rt || !out_rel || !out_unrel) {
        return false;
    }
    return fr_server_net_runtime_client_out_topics(rt, client_id, out_rel, out_unrel);
}

/* ── Unreliable push helper ─────────────────────────────────────── */

static void push_unreliable_(fr_server_net_runtime_t *rt,
                             uint16_t client_id,
                             uint16_t schema_id,
                             const uint8_t *payload,
                             size_t payload_size) {
    if (!rt || !payload || payload_size == 0u) {
        return;
    }

    fr_topic_channel_t *out_rel  = NULL;
    fr_topic_channel_t *out_unrel = NULL;
    if (!fr_server_net_runtime_client_out_topics(rt, client_id,
                                                 &out_rel, &out_unrel) || !out_unrel) {
        return;
    }

    if (payload_size > NET_RUDP_MAX_PACKET_SIZE) {
        return;
    }

    uint8_t msg[2u + NET_RUDP_MAX_PACKET_SIZE];
    msg[0] = (uint8_t)(schema_id & 0xFFu);
    msg[1] = (uint8_t)((schema_id >> 8u) & 0xFFu);
    memcpy(msg + 2u, payload, payload_size);
    (void)fr_topic_channel_push(out_unrel, msg, 2u + payload_size);
}

/* ── Broadcast body states to all connected clients ─────────────── */

static void broadcast_body_states_(fr_server_net_runtime_t *rt,
                                   demo_server_world_t *sw,
                                   const uint8_t *client_connected,
                                   uint16_t max_clients,
                                   uint16_t server_tick) {
    if (!rt || !sw || !client_connected || max_clients == 0u) {
        return;
    }

    /* Iterate all body pool slots and broadcast active dynamic bodies.
     * Skip body 0 (ground plane) and any static/kinematic bodies. */
    const uint32_t capacity = sw->physics.body_pool.capacity;
    for (uint32_t i = 0u; i < capacity; ++i) {
        if (i == DEMO_GROUND_BODY) {
            continue;
        }
        phys_body_t *b = phys_world_get_body(&sw->physics, i);
        if (!b) {
            continue;
        }
        if (phys_body_is_static(b) || phys_body_is_kinematic(b)) {
            continue;
        }

        /* Quantize position. */
        vec3_t pos = VEC3_FROM_PHYS_VEC3(b->position);
        net_qvec3_mm_t qpos;
        if (net_quantize_vec3_mm(pos, &qpos) != NET_QUANT_OK) {
            continue;
        }

        /* Quantize rotation. */
        quat_t rot = QUAT_FROM_PHYS_QUAT(b->orientation);
        net_qquat_snorm16_t qrot;
        if (net_quantize_quat_snorm16(rot, &qrot) != NET_QUANT_OK) {
            continue;
        }

        /* Build STATE_CUBE message. */
        net_repl_state_cube_t st;
        memset(&st, 0, sizeof(st));
        st.server_tick = server_tick;
        st.entity_id   = i;
        st.pos_mm      = (net_repl_vec3_mm_t){qpos.x_mm, qpos.y_mm, qpos.z_mm};
        st.rot_snorm16 = (net_repl_quat_snorm16_t){qrot.x, qrot.y, qrot.z, qrot.w};

        uint8_t payload[NET_REPL_STATE_CUBE_PAYLOAD_SIZE];
        if (net_repl_state_cube_encode(&st, payload, sizeof(payload)) != NET_REPL_OK) {
            continue;
        }

        /* Send to every connected client. */
        for (uint16_t ci = 0u; ci < max_clients; ++ci) {
            if (!client_connected[ci]) {
                continue;
            }
            push_unreliable_(rt, ci, NET_REPL_SCHEMA_STATE_CUBE,
                             payload, sizeof(payload));
        }
    }
}

/* ── Usage ──────────────────────────────────────────────────────── */

static void usage(const char *argv0) {
    fprintf(stderr,
            "Usage: %s <port> [tick_hz] [workers]\n"
            "  Default: port=40080 tick_hz=60 workers=4\n",
            argv0);
}

/* ── Main ───────────────────────────────────────────────────────── */

int main(int argc, char **argv) {
    if (argc < 2 || argc > 4) {
        usage(argv[0]);
        return 2;
    }

    /* Parse arguments. */
    long port_l    = strtol(argv[1], NULL, 10);
    long tick_hz_l = (argc >= 3) ? strtol(argv[2], NULL, 10) : 60;
    long workers_l = (argc >= 4) ? strtol(argv[3], NULL, 10) : 4;

    if (port_l < 1 || port_l > 65535 || tick_hz_l <= 0 || workers_l <= 0) {
        fprintf(stderr, "Invalid arguments\n");
        return 2;
    }

    const uint16_t max_clients = DEMO_MAX_CLIENTS;

    /* ── Signal handling ──────────────────────────────────────────── */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_stop_signal;
    (void)sigaction(SIGINT, &sa, NULL);
    (void)sigaction(SIGTERM, &sa, NULL);

    /* ── UDP socket ───────────────────────────────────────────────── */
    net_udp_socket_t sock;
    if (net_udp_socket_open(&sock) != NET_UDP_SOCKET_OK) {
        fprintf(stderr, "Failed to open UDP socket\n");
        return 1;
    }

    net_udp_addr_t bind_addr;
    if (net_udp_addr_ipv4(&bind_addr, 0u, 0u, 0u, 0u,
                           (uint16_t)port_l) != NET_UDP_SOCKET_OK) {
        fprintf(stderr, "Failed to build bind address\n");
        net_udp_socket_close(&sock);
        return 1;
    }
    if (net_udp_socket_bind(&sock, &bind_addr) != NET_UDP_SOCKET_OK) {
        fprintf(stderr, "Failed to bind port %ld: %s\n", port_l, strerror(errno));
        net_udp_socket_close(&sock);
        return 1;
    }

    (void)net_udp_socket_set_recv_buffer_bytes(&sock, 4u * 1024u * 1024u);
    (void)net_udp_socket_set_send_buffer_bytes(&sock, 4u * 1024u * 1024u);
    (void)net_udp_socket_set_nonblocking(&sock, 1);

    fprintf(stderr, "demo_server: listening on port %ld  tick_hz=%ld  workers=%ld\n",
            port_l, tick_hz_l, workers_l);

    /* ── Job system ───────────────────────────────────────────────── */
    job_system_t jobs;
    job_system_create_status_t jstatus =
        job_system_create(&jobs, (uint32_t)workers_l, 4096u, 1u << 16, 2048, 0);
    if (jstatus != JOB_CREATE_OK) {
        fprintf(stderr, "Failed to create job system\n");
        net_udp_socket_close(&sock);
        return 1;
    }
    if (job_system_start(&jobs) != 0) {
        fprintf(stderr, "Failed to start job system\n");
        job_system_shutdown(&jobs);
        net_udp_socket_close(&sock);
        return 1;
    }

    /* ── Topic channels ───────────────────────────────────────────── */
    fr_topic_channel_config_t tcfg;
    memset(&tcfg, 0, sizeof(tcfg));
    tcfg.capacity = 262144u;

    fr_topic_channel_t *inbound       = fr_topic_channel_create(&tcfg);
    fr_topic_channel_t *player_events = fr_topic_channel_create(&tcfg);
    fr_topic_channel_t *entity_events = fr_topic_channel_create(&tcfg);
    if (!inbound || !player_events || !entity_events) {
        fprintf(stderr, "Failed to allocate topic channels\n");
        fr_topic_channel_destroy(inbound);
        fr_topic_channel_destroy(player_events);
        fr_topic_channel_destroy(entity_events);
        job_system_shutdown(&jobs);
        net_udp_socket_close(&sock);
        return 1;
    }

    /* ── I/O wrapper ──────────────────────────────────────────────── */
    struct demo_server_io io;
    memset(&io, 0, sizeof(io));
    io.sock = &sock;

    /* ── Server net runtime ───────────────────────────────────────── */
    fr_server_net_runtime_config_t rt_cfg;
    memset(&rt_cfg, 0, sizeof(rt_cfg));
    rt_cfg.max_clients            = max_clients;
    rt_cfg.jobs                   = &jobs;
    rt_cfg.socket                 = &sock;
    rt_cfg.inbound_topic          = inbound;
    rt_cfg.out_reliable_capacity  = 8192u;
    rt_cfg.out_unreliable_capacity = 8192u;
    rt_cfg.recvfrom_cb            = recvfrom_counting;
    rt_cfg.sendto_cb              = sendto_counting;
    rt_cfg.io_user                = &io;

    fr_server_net_runtime_t *rt = fr_server_net_runtime_create(&rt_cfg);
    if (!rt) {
        fprintf(stderr, "Failed to create server net runtime\n");
        fr_topic_channel_destroy(inbound);
        fr_topic_channel_destroy(player_events);
        fr_topic_channel_destroy(entity_events);
        job_system_shutdown(&jobs);
        net_udp_socket_close(&sock);
        return 1;
    }

    /* ── Entity net pump ──────────────────────────────────────────── */
    fr_server_entity_net_pump_config_t pump_cfg;
    memset(&pump_cfg, 0, sizeof(pump_cfg));
    pump_cfg.max_clients             = max_clients;
    pump_cfg.tick_hz                 = (uint16_t)tick_hz_l;
    pump_cfg.expected_entities       = DEMO_MAX_BODIES;
    pump_cfg.inbound_topic           = inbound;
    pump_cfg.player_event_topic      = player_events;
    pump_cfg.entity_event_topic      = entity_events;
    pump_cfg.get_client_out_topics_cb = get_out_topics_from_runtime;
    pump_cfg.io_user                 = rt;

    fr_server_entity_net_pump_t *pump = fr_server_entity_net_pump_create(&pump_cfg);
    if (!pump) {
        fprintf(stderr, "Failed to create entity net pump\n");
        fr_server_net_runtime_destroy(rt);
        fr_topic_channel_destroy(inbound);
        fr_topic_channel_destroy(player_events);
        fr_topic_channel_destroy(entity_events);
        job_system_shutdown(&jobs);
        net_udp_socket_close(&sock);
        return 1;
    }

    /* ── Demo server world ────────────────────────────────────────── */
    demo_server_world_t sw;
    if (demo_server_world_init(&sw, 0u) != 0) {
        fprintf(stderr, "Failed to initialize demo server world\n");
        fr_server_entity_net_pump_destroy(pump);
        fr_server_net_runtime_destroy(rt);
        fr_topic_channel_destroy(inbound);
        fr_topic_channel_destroy(player_events);
        fr_topic_channel_destroy(entity_events);
        job_system_shutdown(&jobs);
        net_udp_socket_close(&sock);
        return 1;
    }

    /* Per-client connection tracking. */
    uint8_t client_connected[DEMO_MAX_CLIENTS];
    memset(client_connected, 0, sizeof(client_connected));
    uint32_t clients_joined = 0u;

    /* ── Main loop ────────────────────────────────────────────────── */
    const uint64_t tick_ms  = (uint64_t)(1000u / (uint32_t)tick_hz_l);
    const float    dt_s     = 1.0f / (float)tick_hz_l;
    uint16_t       server_tick = 0u;
    uint64_t       next_tick   = now_ms();

    fprintf(stderr, "DEMO_SERVER_READY\n");

    while (!g_stop) {
        uint64_t now = now_ms();

        /* (a) Receive packets from network. */
        const uint64_t pkts_before = io.packets_recv;
        (void)fr_server_net_runtime_pump(rt, now);
        const int saw_packet = (io.packets_recv != pkts_before);

        /* (b) Route inbound messages through entity pump. */
        (void)fr_server_entity_net_pump_tick(pump, now);

        /* (c) Drain player events (JOIN). */
        for (;;) {
            uint8_t evt[32];
            size_t evt_len = sizeof(evt);
            if (!fr_topic_channel_pop(player_events, evt, &evt_len)) {
                break;
            }
            if (evt_len < 6u) {
                continue;
            }
            if (evt[0] != FR_SERVER_EVT_PLAYER_JOIN) {
                continue;
            }
            uint16_t client_id = (uint16_t)evt[2] | ((uint16_t)evt[3] << 8u);
            if (client_id >= max_clients) {
                continue;
            }
            if (!client_connected[client_id]) {
                client_connected[client_id] = 1u;
                clients_joined += 1u;

                /* Register player in physics world. */
                int slot = demo_server_world_add_player(&sw);
                (void)slot;
                fprintf(stderr, "demo_server: client %u joined (slot %d), %u total\n",
                        (unsigned)client_id, slot, clients_joined);
            }
        }

        /* (c) Drain entity events (INPUT_MOVE, INPUT_SPAWN). */
        for (;;) {
            uint8_t evt[64];
            size_t evt_len = sizeof(evt);
            if (!fr_topic_channel_pop(entity_events, evt, &evt_len)) {
                break;
            }
            if (evt_len < 4u) {
                continue;
            }

            const uint8_t evt_type = evt[0];
            const uint16_t src_client_id = (uint16_t)evt[2] | ((uint16_t)evt[3] << 8u);

            if (src_client_id >= max_clients || !client_connected[src_client_id]) {
                continue;
            }

            const uint8_t *payload   = evt + 4u;
            const size_t payload_len = evt_len - 4u;

            if (evt_type == (uint8_t)FR_SERVER_EVT_ENTITY_INPUT_MOVE) {
                demo_input_move_t input;
                if (demo_input_move_decode(&input, payload, payload_len) != NET_REPL_OK) {
                    continue;
                }
                demo_server_world_apply_input(&sw, (int)src_client_id, &input, dt_s);
            } else if (evt_type == (uint8_t)FR_SERVER_EVT_ENTITY_INPUT_SPAWN) {
                demo_input_spawn_t spawn;
                if (demo_input_spawn_decode(&spawn, payload, payload_len) != NET_REPL_OK) {
                    continue;
                }
                (void)demo_server_world_spawn_box(&sw, (int)src_client_id, &spawn);
            }
            /* Other entity events (INPUT_ROT etc.) are ignored in this demo. */
        }

        /* (d,e,f) Fixed-timestep tick. */
        if (now >= next_tick) {
            server_tick = (uint16_t)(server_tick + 1u);

            /* (d) Tick physics world. */
            demo_server_world_tick(&sw);

            /* (e) Broadcast body states to all connected clients. */
            if (clients_joined > 0u) {
                broadcast_body_states_(rt, &sw, client_connected,
                                       max_clients, server_tick);
            }

            /* Status line every 60 ticks. */
            if ((server_tick % 60u) == 0u) {
                uint32_t body_count = phys_world_body_count(&sw.physics);
                fprintf(stderr, "tick %u: %u bodies, %u clients\n",
                        (unsigned)server_tick, (unsigned)body_count,
                        (unsigned)clients_joined);
            }

            next_tick += tick_ms;
        }

        /* (f) Send outbound packets. */
        (void)fr_server_net_runtime_pump(rt, now_ms());

        /* (g) Yield or sleep until next tick. */
        if (!saw_packet && now_ms() < next_tick) {
            sleep_ms(1u);
        } else {
            sched_yield();
        }
    }

    /* ── Cleanup ──────────────────────────────────────────────────── */
    fprintf(stderr, "demo_server: shutting down (tick %u)\n", (unsigned)server_tick);

    demo_server_world_destroy(&sw);
    fr_server_entity_net_pump_destroy(pump);
    fr_server_net_runtime_destroy(rt);
    fr_topic_channel_destroy(inbound);
    fr_topic_channel_destroy(player_events);
    fr_topic_channel_destroy(entity_events);
    job_system_shutdown(&jobs);
    net_udp_socket_close(&sock);

    fprintf(stderr, "demo_server: clean shutdown. pkts_in=%llu pkts_out=%llu\n",
            (unsigned long long)io.packets_recv,
            (unsigned long long)io.packets_sent);
    return 0;
}
