#define _POSIX_C_SOURCE 200809L

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <errno.h>

#include <math.h>

#include <signal.h>
#include <sched.h>
#include <time.h>

#include "ferrum/job/system.h"
#include "ferrum/math/quat.h"
#include "ferrum/net/rudp/peer.h"
#include "ferrum/net/quantization.h"
#include "ferrum/net/replication/state_cube.h"
#include "ferrum/net/udp_socket.h"
#include "ferrum/net/topic_channel.h"

#include "ferrum/server/entity/net/pump.h"
#include "ferrum/server/net/runtime.h"

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)ts.tv_nsec / 1000000ull;
}

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void sleep_ms(uint32_t ms) {
    struct timespec ts;
    ts.tv_sec = (time_t)(ms / 1000u);
    ts.tv_nsec = (long)((ms % 1000u) * 1000u * 1000u);
    nanosleep(&ts, NULL);
}

static void usage(const char *argv0) {
    fprintf(stderr,
            "Usage: %s <port> <max_clients> <duration_ms> <tick_hz> <workers>\n"
            "  duration_ms=0 runs until SIGINT/SIGTERM\n",
            argv0);
}

static volatile sig_atomic_t g_stop = 0;

static void handle_stop_signal(int signum) {
    (void)signum;
    g_stop = 1;
}

struct p008_server_io {
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
    struct p008_server_io *io = (struct p008_server_io *)user;
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

static int sendto_counting(void *user, const net_udp_addr_t *to, const void *data, size_t size) {
    struct p008_server_io *io = (struct p008_server_io *)user;
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

static void push_unreliable_(fr_server_net_runtime_t *rt,
                             uint16_t client_id,
                             uint16_t schema_id,
                             const uint8_t *payload,
                             size_t payload_size) {
    if (!rt || !payload || payload_size == 0u) {
        return;
    }

    fr_topic_channel_t *out_rel = NULL;
    fr_topic_channel_t *out_unrel = NULL;
    if (!fr_server_net_runtime_client_out_topics(rt, client_id, &out_rel, &out_unrel) || !out_unrel) {
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

static vec3_t cube_pos_(uint16_t owner_client_id, uint16_t max_clients) {
    const float spacing = 0.75f;
    const float center = (max_clients > 0u) ? ((float)(max_clients - 1u) * 0.5f) : 0.0f;
    const float x = ((float)owner_client_id - center) * spacing;
    return (vec3_t){x, 0.0f, 0.0f};
}

static uint32_t xorshift32_(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static float u32_to_float01_(uint32_t x) {
    return (float)((double)x / 4294967295.0);
}

static vec3_t normalize_vec3_safe_(vec3_t v, float eps) {
    const float len2 = v.x * v.x + v.y * v.y + v.z * v.z;
    if (len2 <= eps * eps) {
        return (vec3_t){0.0f, 1.0f, 0.0f};
    }
    const float inv = 1.0f / sqrtf(len2);
    return (vec3_t){v.x * inv, v.y * inv, v.z * inv};
}

static quat_t random_unit_quat_(uint16_t owner_client_id, uint32_t segment) {
    uint32_t rng = (uint32_t)owner_client_id * 2654435761u ^ (segment * 2246822519u) ^ 0xA341316Cu;

    vec3_t axis = {
        u32_to_float01_(xorshift32_(&rng)) * 2.0f - 1.0f,
        u32_to_float01_(xorshift32_(&rng)) * 2.0f - 1.0f,
        u32_to_float01_(xorshift32_(&rng)) * 2.0f - 1.0f,
    };
    axis = normalize_vec3_safe_(axis, 1e-6f);

    const float pi = 3.14159265358979323846f;
    const float angle = u32_to_float01_(xorshift32_(&rng)) * 2.0f * pi;
    const float half = 0.5f * angle;
    const float s = sinf(half);

    quat_t q = {axis.x * s, axis.y * s, axis.z * s, cosf(half)};
    return quat_normalize_safe(q, 1e-6f);
}

static quat_t cube_rot_(uint16_t server_tick, uint16_t owner_client_id, uint16_t tick_hz) {
    if (tick_hz == 0u) {
        return (quat_t){0.0f, 0.0f, 0.0f, 1.0f};
    }

    const uint16_t segment_ticks = tick_hz;
    const uint32_t segment = (uint32_t)(server_tick / segment_ticks);
    const uint16_t local_tick = (uint16_t)(server_tick % segment_ticks);
    const float t = (float)local_tick / (float)segment_ticks;

    const quat_t a = random_unit_quat_(owner_client_id, segment);
    const quat_t b = random_unit_quat_(owner_client_id, segment + 1u);
    return quat_slerp(a, b, t, 1e-6f);
}

static void broadcast_all_states_(fr_server_net_runtime_t *rt,
                                  const uint8_t *joined,
                                  uint16_t max_clients,
                                  uint16_t tick_hz,
                                  uint16_t server_tick) {
    if (!rt || !joined || max_clients == 0u || tick_hz == 0u) {
        return;
    }

    for (uint16_t ci = 0u; ci < max_clients; ++ci) {
        if (!joined[ci]) {
            continue;
        }

        for (uint16_t ei = 0u; ei < max_clients; ++ei) {
            if (!joined[ei]) {
                continue;
            }

            const vec3_t pos = cube_pos_(ei, max_clients);
            const quat_t rot = cube_rot_(server_tick, ei, tick_hz);

            net_qvec3_mm_t qpos;
            if (net_quantize_vec3_mm(pos, &qpos) != NET_QUANT_OK) {
                continue;
            }

            net_qquat_snorm16_t qrot;
            if (net_quantize_quat_snorm16(rot, &qrot) != NET_QUANT_OK) {
                continue;
            }

            net_repl_state_cube_t st;
            st.server_tick = server_tick;
            st.entity_id = 1000u + (uint32_t)ei;
            st.pos_mm = (net_repl_vec3_mm_t){qpos.x_mm, qpos.y_mm, qpos.z_mm};
            st.rot_snorm16 = (net_repl_quat_snorm16_t){qrot.x, qrot.y, qrot.z, qrot.w};

            uint8_t payload[NET_REPL_STATE_CUBE_PAYLOAD_SIZE];
            if (net_repl_state_cube_encode(&st, payload, sizeof(payload)) != NET_REPL_OK) {
                continue;
            }

            push_unreliable_(rt, ci, NET_REPL_SCHEMA_STATE_CUBE, payload, sizeof(payload));
        }
    }
}

int main(int argc, char **argv) {
    if (argc != 6) {
        usage(argv[0]);
        return 2;
    }

    long port_l = strtol(argv[1], NULL, 10);
    long max_clients_l = strtol(argv[2], NULL, 10);
    long duration_ms_l = strtol(argv[3], NULL, 10);
    long tick_hz_l = strtol(argv[4], NULL, 10);
    long workers_l = strtol(argv[5], NULL, 10);

    if (port_l < 0 || port_l > 65535 || max_clients_l <= 0 || duration_ms_l < 0 || tick_hz_l <= 0 || workers_l <= 0) {
        fprintf(stderr, "Invalid arguments\n");
        return 2;
    }

    net_udp_socket_t sock;
    if (net_udp_socket_open(&sock) != NET_UDP_SOCKET_OK) {
        fprintf(stderr, "Failed to open UDP socket\n");
        return 1;
    }

    net_udp_addr_t bind_addr;
    if (net_udp_addr_ipv4(&bind_addr, 0u, 0u, 0u, 0u, (uint16_t)port_l) != NET_UDP_SOCKET_OK) {
        fprintf(stderr, "Failed to build bind address\n");
        net_udp_socket_close(&sock);
        return 1;
    }
    if (net_udp_socket_bind(&sock, &bind_addr) != NET_UDP_SOCKET_OK) {
        fprintf(stderr, "Failed to bind port %ld: %s\n", port_l, strerror(errno));
        net_udp_socket_close(&sock);
        return 1;
    }

    /* Best-effort: larger buffers reduce packet drops under bursty load. */
    (void)net_udp_socket_set_recv_buffer_bytes(&sock, 4u * 1024u * 1024u);
    (void)net_udp_socket_set_send_buffer_bytes(&sock, 4u * 1024u * 1024u);
    (void)net_udp_socket_set_nonblocking(&sock, 1);

    job_system_t jobs;
    job_system_create_status_t status = job_system_create(&jobs, (uint32_t)workers_l, 4096u, 1u << 16, 2048, 0);
    if (status != JOB_CREATE_OK) {
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

    fr_topic_channel_config_t tcfg;
    memset(&tcfg, 0, sizeof(tcfg));
        /* Integration harness: keep channels large to avoid drops when scaling
             client count. This is not a hard product limit.
         */
        tcfg.capacity = 262144u;

    fr_topic_channel_t *inbound = fr_topic_channel_create(&tcfg);
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

    struct p008_server_io io;
    memset(&io, 0, sizeof(io));
    io.sock = &sock;

    fr_server_net_runtime_config_t rt_cfg;
    memset(&rt_cfg, 0, sizeof(rt_cfg));
    rt_cfg.max_clients = (uint16_t)max_clients_l;
    rt_cfg.jobs = &jobs;
    rt_cfg.socket = &sock;
    rt_cfg.inbound_topic = inbound;
    rt_cfg.out_reliable_capacity = 8192u;
    rt_cfg.out_unreliable_capacity = 8192u;
    rt_cfg.recvfrom_cb = recvfrom_counting;
    rt_cfg.sendto_cb = sendto_counting;
    rt_cfg.io_user = &io;

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

    fr_server_entity_net_pump_config_t pump_cfg;
    memset(&pump_cfg, 0, sizeof(pump_cfg));
    pump_cfg.max_clients = (uint16_t)max_clients_l;
    pump_cfg.tick_hz = (uint16_t)tick_hz_l;
    pump_cfg.expected_entities = (uint16_t)max_clients_l;
    pump_cfg.inbound_topic = inbound;
    pump_cfg.player_event_topic = player_events;
    pump_cfg.entity_event_topic = entity_events;
    pump_cfg.get_client_out_topics_cb = get_out_topics_from_runtime;
    pump_cfg.io_user = rt;

    fr_server_entity_net_pump_t *pump = fr_server_entity_net_pump_create(&pump_cfg);
    if (!pump) {
        fprintf(stderr, "Failed to create server entity net pump\n");
        fr_server_net_runtime_destroy(rt);
        fr_topic_channel_destroy(inbound);
        fr_topic_channel_destroy(player_events);
        fr_topic_channel_destroy(entity_events);
        job_system_shutdown(&jobs);
        net_udp_socket_close(&sock);
        return 1;
    }

    uint8_t *joined = (uint8_t *)calloc((size_t)max_clients_l, 1u);
    if (!joined) {
        fprintf(stderr, "Failed to allocate server join/state tracking\n");
        free(joined);
        fr_server_entity_net_pump_destroy(pump);
        fr_server_net_runtime_destroy(rt);
        fr_topic_channel_destroy(inbound);
        fr_topic_channel_destroy(player_events);
        fr_topic_channel_destroy(entity_events);
        job_system_shutdown(&jobs);
        net_udp_socket_close(&sock);
        return 1;
    }
    uint16_t server_tick = 0u;
    uint32_t clients_joined = 0u;

    fprintf(stdout, "P008_REPL_SERVER_READY\n");
    fflush(stdout);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_stop_signal;
    (void)sigaction(SIGINT, &sa, NULL);
    (void)sigaction(SIGTERM, &sa, NULL);

    const uint64_t start = now_ms();
    const uint64_t end = (duration_ms_l == 0) ? UINT64_MAX : (start + (uint64_t)duration_ms_l);
    const uint64_t tick_ms = (uint64_t)(1000u / (uint32_t)tick_hz_l);
    uint64_t next_tick = start;

    uint64_t pump_ns_sum = 0u;
    uint64_t pump_ns_max = 0u;
    uint64_t pump_calls = 0u;

    uint64_t entity_ns_sum = 0u;
    uint64_t entity_ns_max = 0u;
    uint64_t entity_calls = 0u;

    uint64_t broadcast_ns_sum = 0u;
    uint64_t broadcast_ns_max = 0u;
    uint64_t broadcast_calls = 0u;

    uint64_t tick_step_ns_sum = 0u;
    uint64_t tick_step_ns_max = 0u;
    uint64_t tick_steps = 0u;

    uint64_t loop_ns_sum = 0u;
    uint64_t loop_ns_max = 0u;
    uint64_t loop_iters = 0u;

    while (!g_stop && now_ms() < end) {
        const uint64_t loop_start_ns = now_ns();
        uint64_t now = now_ms();

        const uint64_t packets_before = io.packets_recv;
        const uint64_t pump_start_ns = now_ns();
        (void)fr_server_net_runtime_pump(rt, now);
        const uint64_t pump_ns = now_ns() - pump_start_ns;
        pump_ns_sum += pump_ns;
        if (pump_ns > pump_ns_max) {
            pump_ns_max = pump_ns;
        }
        pump_calls += 1u;
        const int saw_packet = (io.packets_recv != packets_before);

        const uint64_t entity_start_ns = now_ns();
        (void)fr_server_entity_net_pump_tick(pump, now);
        const uint64_t entity_ns = now_ns() - entity_start_ns;
        entity_ns_sum += entity_ns;
        if (entity_ns > entity_ns_max) {
            entity_ns_max = entity_ns;
        }
        entity_calls += 1u;

        /* Drain player join events to track connected clients. */
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
            if (client_id >= (uint16_t)max_clients_l) {
                continue;
            }
            if (!joined[client_id]) {
                joined[client_id] = 1u;
                clients_joined += 1u;
            }
        }

        if (now >= next_tick) {
            const uint64_t tick_step_start_ns = now_ns();
            server_tick = (uint16_t)(server_tick + 1u);
            if (clients_joined > 0u) {
                const uint64_t bcast_start_ns = now_ns();
                broadcast_all_states_(rt,
                                      joined,
                                      (uint16_t)max_clients_l,
                                      (uint16_t)tick_hz_l,
                                      server_tick);
                const uint64_t bcast_ns = now_ns() - bcast_start_ns;
                broadcast_ns_sum += bcast_ns;
                if (bcast_ns > broadcast_ns_max) {
                    broadcast_ns_max = bcast_ns;
                }
                broadcast_calls += 1u;
            }
            next_tick += tick_ms;

            const uint64_t tick_step_ns = now_ns() - tick_step_start_ns;
            tick_step_ns_sum += tick_step_ns;
            if (tick_step_ns > tick_step_ns_max) {
                tick_step_ns_max = tick_step_ns;
            }
            tick_steps += 1u;
        }

                /* Yield to worker threads. Sleep only when idle to stay responsive. */
                if (!saw_packet && now < next_tick) {
                    sleep_ms(1u);
                } else {
                    sched_yield();
                }

        const uint64_t loop_ns = now_ns() - loop_start_ns;
        loop_ns_sum += loop_ns;
        if (loop_ns > loop_ns_max) {
            loop_ns_max = loop_ns;
        }
        loop_iters += 1u;
    }

    fprintf(stdout,
            "p008 stats: clients=%u pps_out=%llu pps_in=%llu bytes_out=%llu bytes_in=%llu state_jobs=%llu net_io_ns=%llu state_ns=%llu\n",
            (unsigned)clients_joined,
            (unsigned long long)io.packets_sent,
            (unsigned long long)io.packets_recv,
            (unsigned long long)io.bytes_sent,
            (unsigned long long)io.bytes_recv,
            (unsigned long long)0u,
            (unsigned long long)0u,
            (unsigned long long)0u);

    const uint64_t pump_ns_mean = (pump_calls > 0u) ? (pump_ns_sum / pump_calls) : 0u;
    const uint64_t entity_ns_mean = (entity_calls > 0u) ? (entity_ns_sum / entity_calls) : 0u;
    const uint64_t broadcast_ns_mean = (broadcast_calls > 0u) ? (broadcast_ns_sum / broadcast_calls) : 0u;
    const uint64_t tick_step_ns_mean = (tick_steps > 0u) ? (tick_step_ns_sum / tick_steps) : 0u;
    const uint64_t loop_ns_mean = (loop_iters > 0u) ? (loop_ns_sum / loop_iters) : 0u;

    fprintf(stdout,
            "P008_SERVER_STATS port=%ld max_clients=%ld tick_hz=%ld workers=%ld duration_ms=%ld clients_joined=%u "
            "packets_out=%llu packets_in=%llu bytes_out=%llu bytes_in=%llu "
            "pump_ns_mean=%llu pump_ns_max=%llu entity_ns_mean=%llu entity_ns_max=%llu "
            "broadcast_ns_mean=%llu broadcast_ns_max=%llu tick_ns_mean=%llu tick_ns_max=%llu "
            "loop_ns_mean=%llu loop_ns_max=%llu\n",
            port_l,
            max_clients_l,
            tick_hz_l,
            workers_l,
            duration_ms_l,
            (unsigned)clients_joined,
            (unsigned long long)io.packets_sent,
            (unsigned long long)io.packets_recv,
            (unsigned long long)io.bytes_sent,
            (unsigned long long)io.bytes_recv,
            (unsigned long long)pump_ns_mean,
            (unsigned long long)pump_ns_max,
            (unsigned long long)entity_ns_mean,
            (unsigned long long)entity_ns_max,
            (unsigned long long)broadcast_ns_mean,
            (unsigned long long)broadcast_ns_max,
            (unsigned long long)tick_step_ns_mean,
            (unsigned long long)tick_step_ns_max,
            (unsigned long long)loop_ns_mean,
            (unsigned long long)loop_ns_max);

    free(joined);
    fr_server_entity_net_pump_destroy(pump);
    fr_server_net_runtime_destroy(rt);
    fr_topic_channel_destroy(inbound);
    fr_topic_channel_destroy(player_events);
    fr_topic_channel_destroy(entity_events);
    job_system_shutdown(&jobs);
    net_udp_socket_close(&sock);
    return 0;
}
