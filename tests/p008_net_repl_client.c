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
#include "ferrum/net/replication/state_cube.h"

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
    fprintf(stderr, "Usage: %s <server_ipv4> <port> <duration_ms> <expected_spawns> [tick_hz]\n", argv0);
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
    if (port_l <= 0 || port_l > 65535 || duration_ms_l <= 0 || expected_spawns_l <= 0 || tick_hz_l <= 0 || tick_hz_l > 1000) {
        fprintf(stderr, "Invalid arguments\n");
        return 2;
    }
    const uint16_t tick_hz = (uint16_t)tick_hz_l;

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
    net_rudp_peer_init(&peer, NET_RUDP_PROTOCOL_ID_P008, 50u);

    uint32_t rng = (uint32_t)(0xC001D00Du ^ (uint32_t)getpid());
    net_repl_join_t join;
    join.client_nonce = xorshift32(&rng);
    uint8_t join_payload[NET_REPL_JOIN_PAYLOAD_SIZE];
    if (net_repl_join_encode(&join, join_payload, sizeof(join_payload)) != NET_REPL_OK) {
        fprintf(stderr, "Failed to encode JOIN\n");
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

    uint32_t entity_ids[256];
    size_t entity_count = 0u;
    memset(entity_ids, 0, sizeof(entity_ids));

    uint16_t entity_owner[256];
    memset(entity_owner, 0, sizeof(entity_owner));

    uint64_t pos_err_count = 0u;
    double pos_err_sum = 0.0;
    float pos_err_max = 0.0f;

    uint64_t rot_err_count = 0u;
    double rot_err_sum_deg = 0.0;
    float rot_err_max_deg = 0.0f;

    uint32_t corrections = 0u;

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

        size_t rx_size = 0u;
        int rrc = net_udp_socket_recv(&sock, rx_packet, sizeof(rx_packet), &rx_size);
        if (rrc == NET_UDP_SOCKET_EMPTY) {
            sleep_ms(1u);
            continue;
        }
        if (rrc != NET_UDP_SOCKET_OK) {
            break;
        }

        rx_bytes += (uint64_t)rx_size;
        rx_packets += 1u;

        uint8_t reliable = 0u;
        uint16_t schema_id = 0u;
        uint8_t payload[256];
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

        if (schema_id == NET_REPL_SCHEMA_SPAWN) {
            net_repl_spawn_t sp;
            if (net_repl_spawn_decode(&sp, payload, payload_size) == NET_REPL_OK) {
                spawn_count++;
                if (!has_entity(entity_ids, entity_count, sp.entity_id) && entity_count < 256u) {
                    entity_ids[entity_count++] = sp.entity_id;
                    entity_owner[entity_count - 1u] = sp.owner_client_id;
                }
            }
        } else if (schema_id == NET_REPL_SCHEMA_STATE_CUBE) {
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

    const uint32_t expected_spawns = (uint32_t)expected_spawns_l;
    if (entity_count < (size_t)expected_spawns) {
        fprintf(stderr, "Client failed: expected %u spawns, got %zu (spawn_msgs=%u state_msgs=%u)\n",
                (unsigned)expected_spawns,
                entity_count,
                (unsigned)spawn_count,
                (unsigned)state_count);
        net_udp_socket_close(&sock);
        return 1;
    }
    if (state_count < expected_spawns * 5u) {
        fprintf(stderr, "Client failed: too few state updates (%u)\n", (unsigned)state_count);
        net_udp_socket_close(&sock);
        return 1;
    }

    const double duration_s = (double)duration_ms_l / 1000.0;
    const double rx_mbps = (duration_s > 0.0) ? ((double)rx_bytes * 8.0) / (duration_s * 1000.0 * 1000.0) : 0.0;
    const double tx_mbps = (duration_s > 0.0) ? ((double)tx_bytes * 8.0) / (duration_s * 1000.0 * 1000.0) : 0.0;

    const double pos_mean = (pos_err_count > 0u) ? (pos_err_sum / (double)pos_err_count) : 0.0;
    const double rot_mean = (rot_err_count > 0u) ? (rot_err_sum_deg / (double)rot_err_count) : 0.0;

    fprintf(stdout,
            "P008_CLIENT_STATS tx_bytes=%llu rx_bytes=%llu tx_packets=%llu rx_packets=%llu spawns=%u states=%u "
            "tx_mbps=%.3f rx_mbps=%.3f pos_samples=%llu pos_err_mean=%.6f pos_err_max=%.6f "
            "rot_samples=%llu rot_err_deg_mean=%.6f rot_err_deg_max=%.6f corrections=%u\n",
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
            (unsigned)corrections);
    fflush(stdout);

    net_udp_socket_close(&sock);
    return 0;
}
