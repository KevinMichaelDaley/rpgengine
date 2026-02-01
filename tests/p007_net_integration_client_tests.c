#define _POSIX_C_SOURCE 200809L

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <time.h>

#include "ferrum/net/udp_socket.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#define MAGIC_FRM1 0x46524D31u
#define VERSION_1 1u

enum msg_type {
    MSG_HELLO = 1u,
    MSG_WELCOME = 2u,
    MSG_STATE = 3u,
    MSG_ECHO = 4u,
    MSG_STOP = 5u,
    MSG_STOP_ACK = 6u,
};

static void write_u32_be(uint8_t *out, uint32_t v) {
    out[0] = (uint8_t)((v >> 24) & 0xFFu);
    out[1] = (uint8_t)((v >> 16) & 0xFFu);
    out[2] = (uint8_t)((v >> 8) & 0xFFu);
    out[3] = (uint8_t)(v & 0xFFu);
}

static uint32_t read_u32_be(const uint8_t *in) {
    return ((uint32_t)in[0] << 24) | ((uint32_t)in[1] << 16) | ((uint32_t)in[2] << 8) | (uint32_t)in[3];
}

static void write_i32_be(uint8_t *out, int32_t v) {
    write_u32_be(out, (uint32_t)v);
}

static int32_t read_i32_be(const uint8_t *in) {
    return (int32_t)read_u32_be(in);
}

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

static size_t encode_header(uint8_t *out, size_t cap, uint8_t type) {
    if (!out || cap < 8u) {
        return 0u;
    }
    write_u32_be(out + 0, MAGIC_FRM1);
    out[4] = type;
    out[5] = VERSION_1;
    out[6] = 0u;
    out[7] = 0u;
    return 8u;
}

static int decode_header(const uint8_t *bytes, size_t size, uint8_t *out_type) {
    if (!bytes || size < 8u || !out_type) {
        return 0;
    }
    if (read_u32_be(bytes + 0) != MAGIC_FRM1) {
        return 0;
    }
    if (bytes[5] != VERSION_1) {
        return 0;
    }
    *out_type = bytes[4];
    return 1;
}

struct vec3f {
    float x;
    float y;
    float z;
};

static struct vec3f vec3_add(struct vec3f a, struct vec3f b) {
    return (struct vec3f){a.x + b.x, a.y + b.y, a.z + b.z};
}

static struct vec3f vec3_scale(struct vec3f v, float s) {
    return (struct vec3f){v.x * s, v.y * s, v.z * s};
}

static float vec3_len(struct vec3f v) {
    return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
}

static uint32_t xorshift32(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static float frand01(uint32_t *state) {
    return (float)(xorshift32(state) & 0x00FFFFFFu) / (float)0x01000000u;
}

static float frand_range(uint32_t *state, float lo, float hi) {
    return lo + (hi - lo) * frand01(state);
}

static int32_t quantize_milli(float v) {
    double scaled = (double)v * 1000.0;
    if (scaled > 2147483647.0) {
        return 2147483647;
    }
    if (scaled < -2147483648.0) {
        return (int32_t)0x80000000u;
    }
    return (int32_t)llround(scaled);
}

static float dequantize_milli(int32_t q) {
    return (float)q / 1000.0f;
}

struct state_sample {
    uint32_t seq;
    struct vec3f pos;
    struct vec3f vel;
    uint8_t valid;
};

static const struct state_sample *find_sample(const struct state_sample *ring, size_t ring_count, uint32_t seq) {
    for (size_t i = 0u; i < ring_count; ++i) {
        if (ring[i].valid && ring[i].seq == seq) {
            return &ring[i];
        }
    }
    return NULL;
}

static void usage(const char *argv0) {
    fprintf(stderr, "Usage: %s <server_ipv4> <port>\n", argv0);
}

int main(int argc, char **argv) {
    if (argc != 3) {
        usage(argv[0]);
        return 2;
    }

    uint8_t ip[4];
    if (!parse_ipv4_dotted(argv[1], ip)) {
        fprintf(stderr, "Invalid IPv4 address: %s\n", argv[1]);
        return 2;
    }

    long port_l = strtol(argv[2], NULL, 10);
    if (port_l <= 0 || port_l > 65535) {
        fprintf(stderr, "Invalid port: %s\n", argv[2]);
        return 2;
    }

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
        fprintf(stderr, "Failed to connect UDP socket\n");
        return 1;
    }

    /* Reliable-ish handshake via retries (connection/session logic is test-only). */
    uint32_t rng = 0xC001D00Du;
    uint32_t nonce = xorshift32(&rng);

    uint8_t tx[256];
    uint8_t rx[512];

    uint64_t hello_deadline = now_ms() + 3000ull;
    int welcomed = 0;
    while (!welcomed && now_ms() < hello_deadline) {
        size_t n = encode_header(tx, sizeof(tx), MSG_HELLO);
        write_u32_be(tx + n, nonce);
        n += 4u;
        (void)net_udp_socket_send(&sock, tx, n);

        /* Pump receives for a short window. */
        uint64_t pump_until = now_ms() + 100ull;
        while (now_ms() < pump_until) {
            size_t rx_size = 0u;
            int rc = net_udp_socket_recv(&sock, rx, sizeof(rx), &rx_size);
            if (rc == NET_UDP_SOCKET_EMPTY) {
                sleep_ms(1u);
                continue;
            }
            if (rc != NET_UDP_SOCKET_OK) {
                break;
            }

            uint8_t type = 0u;
            if (!decode_header(rx, rx_size, &type)) {
                continue;
            }
            if (type == MSG_WELCOME && rx_size >= 12u) {
                uint32_t got = read_u32_be(rx + 8);
                if (got == nonce) {
                    welcomed = 1;
                    break;
                }
            }
        }
    }

    if (!welcomed) {
        fprintf(stderr, "Handshake failed (no WELCOME)\n");
        net_udp_socket_close(&sock);
        return 1;
    }

    /* Trajectory sim: intermittent random acceleration; send quantized pos/vel unreliably, expect echoed packets. */
    const float dt = 1.0f / 60.0f;
    const uint32_t steps = 600u; /* 10 seconds at 60Hz */

    struct vec3f pos = {0};
    struct vec3f vel = {0};
    struct vec3f acc = {0};

    struct state_sample ring[2048];
    memset(ring, 0, sizeof(ring));

    uint32_t received = 0u;
    float max_pos_err = 0.0f;
    float max_vel_err = 0.0f;

    for (uint32_t seq = 1u; seq <= steps; ++seq) {
        /* Randomly update acceleration every ~250ms. */
        if ((seq % 15u) == 1u) {
            acc.x = frand_range(&rng, -5.0f, 5.0f);
            acc.y = frand_range(&rng, -5.0f, 5.0f);
            acc.z = frand_range(&rng, -2.0f, 2.0f);
        }

        vel = vec3_add(vel, vec3_scale(acc, dt));
        pos = vec3_add(pos, vec3_scale(vel, dt));

        ring[seq % ARRAY_SIZE(ring)] = (struct state_sample){
            .seq = seq,
            .pos = pos,
            .vel = vel,
            .valid = 1u,
        };

        /* Build STATE packet. */
        size_t n = encode_header(tx, sizeof(tx), MSG_STATE);
        write_u32_be(tx + n, seq);
        n += 4u;

        int32_t qpx = quantize_milli(pos.x);
        int32_t qpy = quantize_milli(pos.y);
        int32_t qpz = quantize_milli(pos.z);
        int32_t qvx = quantize_milli(vel.x);
        int32_t qvy = quantize_milli(vel.y);
        int32_t qvz = quantize_milli(vel.z);

        write_i32_be(tx + n, qpx);
        n += 4u;
        write_i32_be(tx + n, qpy);
        n += 4u;
        write_i32_be(tx + n, qpz);
        n += 4u;
        write_i32_be(tx + n, qvx);
        n += 4u;
        write_i32_be(tx + n, qvy);
        n += 4u;
        write_i32_be(tx + n, qvz);
        n += 4u;

        (void)net_udp_socket_send(&sock, tx, n);

        /* Pump echoed packets (nonblocking). */
        for (uint32_t spin = 0u; spin < 4u; ++spin) {
            size_t rx_size = 0u;
            int rc = net_udp_socket_recv(&sock, rx, sizeof(rx), &rx_size);
            if (rc == NET_UDP_SOCKET_EMPTY) {
                break;
            }
            if (rc != NET_UDP_SOCKET_OK) {
                break;
            }

            uint8_t type = 0u;
            if (!decode_header(rx, rx_size, &type)) {
                continue;
            }
            if (type != MSG_ECHO) {
                continue;
            }
            if (rx_size < 8u + 4u + 24u) {
                continue;
            }

            uint32_t echo_seq = read_u32_be(rx + 8);
            int32_t e_qpx = read_i32_be(rx + 12);
            int32_t e_qpy = read_i32_be(rx + 16);
            int32_t e_qpz = read_i32_be(rx + 20);
            int32_t e_qvx = read_i32_be(rx + 24);
            int32_t e_qvy = read_i32_be(rx + 28);
            int32_t e_qvz = read_i32_be(rx + 32);

            struct vec3f epos = {dequantize_milli(e_qpx), dequantize_milli(e_qpy), dequantize_milli(e_qpz)};
            struct vec3f evel = {dequantize_milli(e_qvx), dequantize_milli(e_qvy), dequantize_milli(e_qvz)};

            const struct state_sample *truth = find_sample(ring, ARRAY_SIZE(ring), echo_seq);
            if (!truth) {
                continue;
            }

            struct vec3f pos_err_v = (struct vec3f){epos.x - truth->pos.x, epos.y - truth->pos.y, epos.z - truth->pos.z};
            struct vec3f vel_err_v = (struct vec3f){evel.x - truth->vel.x, evel.y - truth->vel.y, evel.z - truth->vel.z};

            float pos_err = vec3_len(pos_err_v);
            float vel_err = vec3_len(vel_err_v);

            if (pos_err > max_pos_err) {
                max_pos_err = pos_err;
            }
            if (vel_err > max_vel_err) {
                max_vel_err = vel_err;
            }

            received++;
        }

        sleep_ms(16u);
    }

    /* Drain a bit for late echoes. */
    uint64_t drain_until = now_ms() + 500ull;
    while (now_ms() < drain_until) {
        size_t rx_size = 0u;
        int rc = net_udp_socket_recv(&sock, rx, sizeof(rx), &rx_size);
        if (rc == NET_UDP_SOCKET_EMPTY) {
            sleep_ms(1u);
            continue;
        }
        if (rc != NET_UDP_SOCKET_OK) {
            break;
        }

        uint8_t type = 0u;
        if (!decode_header(rx, rx_size, &type)) {
            continue;
        }
        if (type != MSG_ECHO || rx_size < 36u) {
            continue;
        }

        uint32_t echo_seq = read_u32_be(rx + 8);
        int32_t e_qpx = read_i32_be(rx + 12);
        int32_t e_qpy = read_i32_be(rx + 16);
        int32_t e_qpz = read_i32_be(rx + 20);
        int32_t e_qvx = read_i32_be(rx + 24);
        int32_t e_qvy = read_i32_be(rx + 28);
        int32_t e_qvz = read_i32_be(rx + 32);

        struct vec3f epos = {dequantize_milli(e_qpx), dequantize_milli(e_qpy), dequantize_milli(e_qpz)};
        struct vec3f evel = {dequantize_milli(e_qvx), dequantize_milli(e_qvy), dequantize_milli(e_qvz)};

        const struct state_sample *truth = find_sample(ring, ARRAY_SIZE(ring), echo_seq);
        if (!truth) {
            continue;
        }

        struct vec3f pos_err_v = (struct vec3f){epos.x - truth->pos.x, epos.y - truth->pos.y, epos.z - truth->pos.z};
        struct vec3f vel_err_v = (struct vec3f){evel.x - truth->vel.x, evel.y - truth->vel.y, evel.z - truth->vel.z};

        float pos_err = vec3_len(pos_err_v);
        float vel_err = vec3_len(vel_err_v);

        if (pos_err > max_pos_err) {
            max_pos_err = pos_err;
        }
        if (vel_err > max_vel_err) {
            max_vel_err = vel_err;
        }

        received++;
    }

    /* Send STOP reliably-ish. */
    uint64_t stop_deadline = now_ms() + 2000ull;
    int stopped = 0;
    while (!stopped && now_ms() < stop_deadline) {
        size_t n = encode_header(tx, sizeof(tx), MSG_STOP);
        write_u32_be(tx + n, nonce);
        n += 4u;
        (void)net_udp_socket_send(&sock, tx, n);

        uint64_t pump_until = now_ms() + 100ull;
        while (now_ms() < pump_until) {
            size_t rx_size = 0u;
            int rc = net_udp_socket_recv(&sock, rx, sizeof(rx), &rx_size);
            if (rc == NET_UDP_SOCKET_EMPTY) {
                sleep_ms(1u);
                continue;
            }
            if (rc != NET_UDP_SOCKET_OK) {
                break;
            }
            uint8_t type = 0u;
            if (!decode_header(rx, rx_size, &type)) {
                continue;
            }
            if (type == MSG_STOP_ACK && rx_size >= 12u) {
                uint32_t got = read_u32_be(rx + 8);
                if (got == nonce) {
                    stopped = 1;
                    break;
                }
            }
        }
    }

    net_udp_socket_close(&sock);

    const float min_echo_ratio = 0.20f; /* allow heavy loss on real networks */
    float ratio = (float)received / (float)steps;

    /* Quantization is 1mm per component; vector magnitude error should stay small. */
    const float max_allowed_pos_err = 0.005f;
    const float max_allowed_vel_err = 0.005f;

    printf("echoes_received=%u steps=%u ratio=%.3f max_pos_err=%.6f max_vel_err=%.6f\n",
           received, steps, ratio, max_pos_err, max_vel_err);

    if (ratio < min_echo_ratio) {
        fprintf(stderr, "Too few echoes received\n");
        return 1;
    }
    if (max_pos_err > max_allowed_pos_err) {
        fprintf(stderr, "Position reconstruction error too high\n");
        return 1;
    }
    if (max_vel_err > max_allowed_vel_err) {
        fprintf(stderr, "Velocity reconstruction error too high\n");
        return 1;
    }

    if (!stopped) {
        /* Not fatal for the integration metric; server may be configured differently. */
        fprintf(stderr, "Warning: did not receive STOP_ACK\n");
    }

    return 0;
}
