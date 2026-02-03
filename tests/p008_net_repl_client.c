#define _POSIX_C_SOURCE 200809L

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <math.h>
#include <string.h>

#include <time.h>

#include <unistd.h>

#include "ferrum/net/packet_header.h"
#include "ferrum/net/quantization.h"
#include "ferrum/net/udp_socket.h"
#include "ferrum/net/rudp/peer.h"
#include "ferrum/net/replication/join.h"
#include "ferrum/net/replication/spawn.h"
#include "ferrum/net/replication/spawn_batch.h"
#include "ferrum/net/replication/state_cube.h"
#include "ferrum/net/replication/welcome.h"

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)ts.tv_nsec / 1000000ull;
}

static void sleep_ms(uint32_t ms) {
    struct timespec ts;
    ts.tv_sec = (time_t)(ms / 1000u);
    ts.tv_nsec = (long)((ms % 1000u) * 1000u * 1000u);
    nanosleep(&ts, NULL);
}

static int parse_ipv4_dotted(const char *s, uint8_t out[4]) {
    unsigned a = 0, b = 0, c = 0, d = 0;
    if (!s) {
        return 0;
    }
    if (sscanf(s, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) {
        return 0;
    }
    if (a > 255u || b > 255u || c > 255u || d > 255u) {
        return 0;
    }
    out[0] = (uint8_t)a;
    out[1] = (uint8_t)b;
    out[2] = (uint8_t)c;
    out[3] = (uint8_t)d;
    return 1;
}

static void usage(const char *argv0) {
    fprintf(stderr, "Usage: %s <server_ipv4> <port> <duration_ms> <expected_spawns|0> [tick_hz]\n", argv0);
}

static uint32_t xorshift32(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static int has_entity(const uint32_t *ids, size_t count, uint32_t id) {
    for (size_t i = 0u; i < count; ++i) {
        if (ids[i] == id) {
            return 1;
        }
    }
    return 0;
}

static int ensure_entity_capacity(uint32_t **ids,
                                  uint16_t **owners,
                                  size_t *capacity,
                                  size_t needed) {
    if (!ids || !owners || !capacity) {
        return 0;
    }
    if (needed <= *capacity) {
        return 1;
    }

    size_t new_cap = (*capacity == 0u) ? 16u : *capacity;
    while (new_cap < needed) {
        new_cap *= 2u;
    }

    uint32_t *new_ids = (uint32_t *)realloc(*ids, new_cap * sizeof(**ids));
    uint16_t *new_owners = (uint16_t *)realloc(*owners, new_cap * sizeof(**owners));
    if (!new_ids || !new_owners) {
        free(new_ids);
        free(new_owners);
        return 0;
    }

    /* Zero-init new tail to preserve has_entity semantics with default zeros. */
    if (new_cap > *capacity) {
        const size_t old_cap = *capacity;
        memset(new_ids + old_cap, 0, (new_cap - old_cap) * sizeof(*new_ids));
        memset(new_owners + old_cap, 0, (new_cap - old_cap) * sizeof(*new_owners));
    }

    *ids = new_ids;
    *owners = new_owners;
    *capacity = new_cap;
    return 1;
}

static float clampf(float x, float lo, float hi) {
    if (x < lo) {
        return lo;
    }
    if (x > hi) {
        return hi;
    }
    return x;
}

static float vec3_distance(vec3_t a, vec3_t b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    const float dz = a.z - b.z;
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

static vec3_t expected_pos(uint16_t server_tick, uint16_t owner_client_id, uint16_t tick_hz) {
    const float t = (float)server_tick / (float)tick_hz;
    const float phase = (float)owner_client_id * 0.25f;
    return (vec3_t){cosf(t + phase), 0.0f, sinf(t + phase)};
}

static float rot_error_degrees_identity(quat_t q) {
    /* identity dot is q.w; canonicalization keeps w >= 0 */
    const float w = clampf(q.w, -1.0f, 1.0f);
    const float angle_rad = 2.0f * acosf(w);
    return angle_rad * (180.0f / 3.14159265358979323846f);
}

int main(int argc, char **argv) {
    if (argc != 5 && argc != 6) {
        usage(argv[0]);
        return 2;
    }

    uint8_t ip[4];
    if (!parse_ipv4_dotted(argv[1], ip)) {
        fprintf(stderr, "Invalid IPv4 address: %s\n", argv[1]);
        return 2;
    }
    long port_l = strtol(argv[2], NULL, 10);
    long duration_ms_l = strtol(argv[3], NULL, 10);
    long expected_spawns_l = strtol(argv[4], NULL, 10);
    long tick_hz_l = 60;
    if (argc == 6) {
        tick_hz_l = strtol(argv[5], NULL, 10);
    }
    if (port_l <= 0 || port_l > 65535 || duration_ms_l <= 0 || expected_spawns_l < 0 || tick_hz_l <= 0 || tick_hz_l > 1000) {
        fprintf(stderr, "Invalid arguments\n");
        return 2;
    }
    uint16_t tick_hz = (uint16_t)tick_hz_l;
    uint32_t expected_spawns = (uint32_t)expected_spawns_l;

    net_udp_addr_t server_addr;
    if (net_udp_addr_ipv4(&server_addr, ip[0], ip[1], ip[2], ip[3], (uint16_t)port_l) != NET_UDP_SOCKET_OK) {
        fprintf(stderr, "Failed to build server address\n");
        return 1;
    }

    net_udp_socket_t sock;
    if (net_udp_socket_open(&sock) != NET_UDP_SOCKET_OK) {
        fprintf(stderr, "Failed to open UDP socket\n");
        return 1;
    }
    /* Best-effort: larger buffers reduce drops under bursty state traffic. */
    (void)net_udp_socket_set_recv_buffer_bytes(&sock, 4u * 1024u * 1024u);
    (void)net_udp_socket_set_send_buffer_bytes(&sock, 4u * 1024u * 1024u);
    (void)net_udp_socket_set_nonblocking(&sock, 1);
    if (net_udp_socket_connect(&sock, &server_addr) != NET_UDP_SOCKET_OK) {
        fprintf(stderr, "Failed to connect\n");
        net_udp_socket_close(&sock);
        return 1;
    }

    uint64_t tx_bytes = 0u;
    uint64_t rx_bytes = 0u;
    uint64_t tx_packets = 0u;
    uint64_t rx_packets = 0u;

    net_rudp_peer_t peer;
    const size_t rudp_send_slot_count = (size_t)NET_RUDP_SEND_SLOTS_DEFAULT;
    const size_t rudp_send_slot_bytes = net_rudp_send_slot_storage_size(rudp_send_slot_count);
    net_rudp_send_slot_t *rudp_send_slots = (net_rudp_send_slot_t *)calloc(1u, rudp_send_slot_bytes);
    if (!rudp_send_slots) {
        fprintf(stderr, "Failed to allocate RUDP send slots\n");
        net_udp_socket_close(&sock);
        return 1;
    }
    net_rudp_peer_init_with_storage(&peer, NET_RUDP_PROTOCOL_ID_P008, 50u, rudp_send_slots, rudp_send_slot_count);

    uint32_t rng = (uint32_t)(0xC001D00Du ^ (uint32_t)getpid());
    net_repl_join_t join;
    join.client_nonce = xorshift32(&rng);
    uint8_t join_payload[NET_REPL_JOIN_PAYLOAD_SIZE];
    if (net_repl_join_encode(&join, join_payload, sizeof(join_payload)) != NET_REPL_OK) {
        fprintf(stderr, "Failed to encode JOIN\n");
        free(rudp_send_slots);
        net_udp_socket_close(&sock);
        return 1;
    }

    uint16_t join_seq = 0u;
    if (net_rudp_peer_send_reliable(&peer,
                                   &sock,
                                   &server_addr,
                                   now_ms(),
                                   NET_REPL_SCHEMA_JOIN,
                                   join_payload,
                                   sizeof(join_payload),
                                   &join_seq) != NET_RUDP_OK) {
        fprintf(stderr, "Failed to send JOIN\n");
        free(rudp_send_slots);
        net_udp_socket_close(&sock);
        return 1;
    }
    tx_bytes += (uint64_t)(NET_PACKET_HEADER_SIZE + 8u + sizeof(join_payload));
    tx_packets += 1u;
    (void)join_seq;

    const uint64_t start = now_ms();
    const uint64_t end = start + (uint64_t)duration_ms_l;
    uint64_t next_keepalive = start;
    uint32_t spawn_count = 0u;
    uint32_t state_count = 0u;

    uint32_t *entity_ids = NULL;
    uint16_t *entity_owner = NULL;
    size_t entity_capacity = 0u;
    if (expected_spawns > 0u) {
        entity_ids = (uint32_t *)calloc((size_t)expected_spawns, sizeof(*entity_ids));
        entity_owner = (uint16_t *)calloc((size_t)expected_spawns, sizeof(*entity_owner));
        if (!entity_ids || !entity_owner) {
            fprintf(stderr, "Failed to allocate entity tracking arrays\n");
            free(entity_ids);
            free(entity_owner);
            free(rudp_send_slots);
            net_udp_socket_close(&sock);
            return 1;
        }
    }
    size_t entity_count = 0u;

    uint64_t pos_err_count = 0u;
    double pos_err_sum = 0.0;
    float pos_err_max = 0.0f;

    uint64_t rot_err_count = 0u;
    double rot_err_sum_deg = 0.0;
    float rot_err_max_deg = 0.0f;

    uint32_t corrections = 0u;

    /* State update latency instrumentation (inter-arrival). */
    uint64_t last_state_ms = 0u;
    uint64_t state_delta_count = 0u;
    double state_delta_sum_ms = 0.0;
    double state_delta_max_ms = 0.0;
    double state_lag_over_expected_sum_ms = 0.0;
    double state_lag_over_expected_max_ms = 0.0;

    uint8_t rx_packet[NET_RUDP_MAX_PACKET_SIZE];
    while (now_ms() < end) {
        uint64_t now = now_ms();

        if (now >= next_keepalive) {
            (void)net_rudp_peer_send_unreliable(&peer,
                                                &sock,
                                                &server_addr,
                                                now,
                                                NET_REPL_SCHEMA_JOIN,
                                                join_payload,
                                                sizeof(join_payload));
            tx_bytes += (uint64_t)(NET_PACKET_HEADER_SIZE + 8u + sizeof(join_payload));
            tx_packets += 1u;
            next_keepalive = now + 100u;
        }

        (void)net_rudp_peer_tick_resend(&peer, &sock, &server_addr, now);

        int saw_rx = 0;
        for (;;) {
            size_t rx_size = 0u;
            int rrc = net_udp_socket_recv(&sock, rx_packet, sizeof(rx_packet), &rx_size);
            if (rrc == NET_UDP_SOCKET_EMPTY) {
                break;
            }
            if (rrc != NET_UDP_SOCKET_OK) {
                goto done;
            }

            saw_rx = 1;
            rx_bytes += (uint64_t)rx_size;
            rx_packets += 1u;

            uint8_t reliable = 0u;
            uint16_t schema_id = 0u;
            uint8_t payload[NET_RUDP_MAX_PACKET_SIZE];
            size_t payload_size = 0u;
            int prc = net_rudp_peer_receive(&peer,
                                            rx_packet,
                                            rx_size,
                                            &reliable,
                                            &schema_id,
                                            payload,
                                            sizeof(payload),
                                            &payload_size);
            if (prc != NET_RUDP_OK) {
                continue;
            }
            (void)reliable;

            if (schema_id == NET_REPL_SCHEMA_WELCOME) {
            net_repl_welcome_t w;
            if (net_repl_welcome_decode(&w, payload, payload_size) == NET_REPL_OK) {
                if (w.tick_hz > 0u && w.tick_hz <= 1000u) {
                    tick_hz = w.tick_hz;
                }
                if (expected_spawns == 0u && w.expected_entities > 0u) {
                    expected_spawns = (uint32_t)w.expected_entities;
                    if (!ensure_entity_capacity(&entity_ids, &entity_owner, &entity_capacity, (size_t)expected_spawns)) {
                        fprintf(stderr, "Failed to allocate entity tracking arrays\n");
                        free(entity_ids);
                        free(entity_owner);
                        free(rudp_send_slots);
                        net_udp_socket_close(&sock);
                        return 1;
                    }
                }
            }
            } else if (schema_id == NET_REPL_SCHEMA_SPAWN) {
            net_repl_spawn_t sp;
            if (net_repl_spawn_decode(&sp, payload, payload_size) == NET_REPL_OK) {
                spawn_count++;
                if (!has_entity(entity_ids, entity_count, sp.entity_id)) {
                    if (!ensure_entity_capacity(&entity_ids, &entity_owner, &entity_capacity, entity_count + 1u)) {
                        fprintf(stderr, "Failed to grow entity tracking arrays\n");
                        free(entity_ids);
                        free(entity_owner);
                        free(rudp_send_slots);
                        net_udp_socket_close(&sock);
                        return 1;
                    }
                    entity_ids[entity_count++] = sp.entity_id;
                    entity_owner[entity_count - 1u] = sp.owner_client_id;
                }
            }
            } else if (schema_id == NET_REPL_SCHEMA_SPAWN_BATCH) {
            net_repl_spawn_batch_entry_t entries[32];
            uint16_t count = 0u;
            uint16_t tick = 0u;
            if (net_repl_spawn_batch_decode(&tick, entries, 32u, &count, payload, payload_size) == NET_REPL_OK) {
                (void)tick;
                spawn_count += (uint32_t)count;
                for (uint16_t i = 0u; i < count; ++i) {
                    const net_repl_spawn_batch_entry_t *e = &entries[i];
                    if (has_entity(entity_ids, entity_count, e->entity_id)) {
                        continue;
                    }
                    if (!ensure_entity_capacity(&entity_ids, &entity_owner, &entity_capacity, entity_count + 1u)) {
                        fprintf(stderr, "Failed to grow entity tracking arrays\n");
                        free(entity_ids);
                        free(entity_owner);
                        free(rudp_send_slots);
                        net_udp_socket_close(&sock);
                        return 1;
                    }
                    entity_ids[entity_count++] = e->entity_id;
                    entity_owner[entity_count - 1u] = e->owner_client_id;
                }
            }
            } else if (schema_id == NET_REPL_SCHEMA_STATE_CUBE) {
            const uint64_t recv_now = now_ms();
            if (last_state_ms > 0u) {
                const double delta_ms = (double)(recv_now - last_state_ms);
                state_delta_sum_ms += delta_ms;
                state_delta_count++;
                if (delta_ms > state_delta_max_ms) {
                    state_delta_max_ms = delta_ms;
                }
                const double expected_ms = 1000.0 / (double)tick_hz;
                const double lag_ms = (delta_ms > expected_ms) ? (delta_ms - expected_ms) : 0.0;
                state_lag_over_expected_sum_ms += lag_ms;
                if (lag_ms > state_lag_over_expected_max_ms) {
                    state_lag_over_expected_max_ms = lag_ms;
                }
            }
            last_state_ms = recv_now;
            net_repl_state_cube_t st;
            if (net_repl_state_cube_decode(&st, payload, payload_size) == NET_REPL_OK) {
                state_count++;

                int found = 0;
                uint16_t owner = 0u;
                for (size_t i = 0u; i < entity_count; ++i) {
                    if (entity_ids[i] == st.entity_id) {
                        owner = entity_owner[i];
                        found = 1;
                        break;
                    }
                }
                if (found) {
                    net_qvec3_mm_t qv;
                    qv.x_mm = st.pos_mm.x_mm;
                    qv.y_mm = st.pos_mm.y_mm;
                    qv.z_mm = st.pos_mm.z_mm;
                    qv._magic = 0x4D4D3351u; /* 'Q3MM' */

                    vec3_t pos;
                    if (net_dequantize_vec3_mm(qv, &pos) == NET_QUANT_OK) {
                        vec3_t exp = expected_pos(st.server_tick, owner, tick_hz);
                        const float err = vec3_distance(pos, exp);
                        pos_err_sum += (double)err;
                        pos_err_count++;
                        if (err > pos_err_max) {
                            pos_err_max = err;
                        }
                    }

                    net_qquat_snorm16_t qq;
                    qq.x = st.rot_snorm16.x;
                    qq.y = st.rot_snorm16.y;
                    qq.z = st.rot_snorm16.z;
                    qq.w = st.rot_snorm16.w;
                    qq._magic = 0x4E513136u; /* 'NQ16' */

                    quat_t rot;
                    if (net_dequantize_quat_snorm16(qq, &rot) == NET_QUANT_OK) {
                        const float deg = rot_error_degrees_identity(rot);
                        rot_err_sum_deg += (double)deg;
                        rot_err_count++;
                        if (deg > rot_err_max_deg) {
                            rot_err_max_deg = deg;
                        }
                    }
                }
            }
            }
        }

        if (!saw_rx) {
            sleep_ms(1u);
        }
    }

done:

    if (expected_spawns == 0u || !entity_ids || !entity_owner) {
        fprintf(stderr, "Client failed: did not receive WELCOME/expected_spawns\n");
        free(entity_ids);
        free(entity_owner);
        free(rudp_send_slots);
        net_udp_socket_close(&sock);
        return 1;
    }

    if (entity_count < (size_t)expected_spawns) {
        fprintf(stderr, "Client failed: expected %u spawns, got %zu (spawn_msgs=%u state_msgs=%u)\n",
                (unsigned)expected_spawns,
                entity_count,
                (unsigned)spawn_count,
                (unsigned)state_count);
        free(entity_ids);
        free(entity_owner);
        free(rudp_send_slots);
        net_udp_socket_close(&sock);
        return 1;
    }
    if (state_count < expected_spawns * 5u) {
        fprintf(stderr, "Client failed: too few state updates (%u)\n", (unsigned)state_count);
        free(entity_ids);
        free(entity_owner);
        free(rudp_send_slots);
        net_udp_socket_close(&sock);
        return 1;
    }

    const double duration_s = (double)duration_ms_l / 1000.0;
    const double rx_mbps = (duration_s > 0.0) ? ((double)rx_bytes * 8.0) / (duration_s * 1000.0 * 1000.0) : 0.0;
    const double tx_mbps = (duration_s > 0.0) ? ((double)tx_bytes * 8.0) / (duration_s * 1000.0 * 1000.0) : 0.0;

    const double pos_mean = (pos_err_count > 0u) ? (pos_err_sum / (double)pos_err_count) : 0.0;
    const double rot_mean = (rot_err_count > 0u) ? (rot_err_sum_deg / (double)rot_err_count) : 0.0;
    const double state_inter_mean_ms = (state_delta_count > 0u) ? (state_delta_sum_ms / (double)state_delta_count) : 0.0;
    const double state_lag_mean_ms = (state_delta_count > 0u) ? (state_lag_over_expected_sum_ms / (double)state_delta_count) : 0.0;

        fprintf(stdout,
            "P008_CLIENT_STATS tx_bytes=%llu rx_bytes=%llu tx_packets=%llu rx_packets=%llu spawns=%u states=%u "
            "tx_mbps=%.3f rx_mbps=%.3f pos_samples=%llu pos_err_mean=%.6f pos_err_max=%.6f "
            "rot_samples=%llu rot_err_deg_mean=%.6f rot_err_deg_max=%.6f corrections=%u "
            "state_inter_ms_mean=%.3f state_inter_ms_max=%.3f state_lag_ms_mean=%.3f state_lag_ms_max=%.3f\n",
            (unsigned long long)tx_bytes,
            (unsigned long long)rx_bytes,
            (unsigned long long)tx_packets,
            (unsigned long long)rx_packets,
            (unsigned)spawn_count,
            (unsigned)state_count,
            tx_mbps,
            rx_mbps,
            (unsigned long long)pos_err_count,
            pos_mean,
            pos_err_max,
            (unsigned long long)rot_err_count,
            rot_mean,
            rot_err_max_deg,
            (unsigned)corrections,
            state_inter_mean_ms,
            state_delta_max_ms,
            state_lag_mean_ms,
            state_lag_over_expected_max_ms);
    fflush(stdout);

    free(entity_ids);
    free(entity_owner);
    free(rudp_send_slots);
    net_udp_socket_close(&sock);
    return 0;
}
