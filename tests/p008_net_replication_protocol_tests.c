#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/net/replication/join.h"
#include "ferrum/net/replication/spawn.h"
#include "ferrum/net/replication/spawn_batch.h"
#include "ferrum/net/replication/state_cube.h"
#include "ferrum/net/replication/welcome.h"

#define ASSERT_TRUE(cond)                                                                                \
    do {                                                                                                 \
        if (!(cond)) {                                                                                   \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n", __FILE__, __LINE__, #cond);               \
            return 1;                                                                                    \
        }                                                                                                \
    } while (0)

#define ASSERT_INT_EQ(exp, act)                                                                          \
    do {                                                                                                 \
        if ((int)(exp) != (int)(act)) {                                                                  \
            fprintf(stderr, "ASSERT_INT_EQ failed: %s:%d: expected %d got %d\n", __FILE__, __LINE__,     \
                    (int)(exp), (int)(act));                                                             \
            return 1;                                                                                    \
        }                                                                                                \
    } while (0)

#define ASSERT_UINT_EQ(exp, act)                                                                         \
    do {                                                                                                 \
        if ((uint64_t)(exp) != (uint64_t)(act)) {                                                         \
            fprintf(stderr, "ASSERT_UINT_EQ failed: %s:%d: expected %llu got %llu\n", __FILE__,          \
                    __LINE__, (unsigned long long)(exp), (unsigned long long)(act));                     \
            return 1;                                                                                    \
        }                                                                                                \
    } while (0)

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static int test_join_encode_byte_layout_lockin(void) {
    /* Layout contract (big-endian):
     *  [0..3] client_nonce (u32)
     */
    const net_repl_join_t msg = {
        .client_nonce = 0x11223344u,
    };

    uint8_t payload[NET_REPL_JOIN_PAYLOAD_SIZE];
    ASSERT_INT_EQ(NET_REPL_OK, net_repl_join_encode(&msg, payload, sizeof(payload)));

    const uint8_t expected[NET_REPL_JOIN_PAYLOAD_SIZE] = {0x11u, 0x22u, 0x33u, 0x44u};
    ASSERT_TRUE(memcmp(expected, payload, sizeof(expected)) == 0);

    net_repl_join_t decoded = {0};
    ASSERT_INT_EQ(NET_REPL_OK, net_repl_join_decode(&decoded, payload, sizeof(payload)));
    ASSERT_UINT_EQ(msg.client_nonce, decoded.client_nonce);
    return 0;
}

static int test_spawn_encode_byte_layout_lockin(void) {
    /* Layout contract (big-endian):
     *  [0..3]  entity_id (u32)
     *  [4..5]  owner_client_id (u16)
     *  [6..7]  join_time_u16 (u16)
     *  [8..19] pos_mm xyz (i32,i32,i32)
     */
    const net_repl_spawn_t msg = {
        .entity_id = 0xA1B2C3D4u,
        .owner_client_id = 0x1357u,
        .join_time_u16 = 0x2468u,
        .pos_mm = {.x_mm = 1000, .y_mm = -2000, .z_mm = 3000},
    };

    uint8_t payload[NET_REPL_SPAWN_PAYLOAD_SIZE];
    ASSERT_INT_EQ(NET_REPL_OK, net_repl_spawn_encode(&msg, payload, sizeof(payload)));

    const uint8_t expected[NET_REPL_SPAWN_PAYLOAD_SIZE] = {
        0xA1u, 0xB2u, 0xC3u, 0xD4u,
        0x13u, 0x57u,
        0x24u, 0x68u,
        0x00u, 0x00u, 0x03u, 0xE8u, /* 1000 */
        0xFFu, 0xFFu, 0xF8u, 0x30u, /* -2000 */
        0x00u, 0x00u, 0x0Bu, 0xB8u, /* 3000 */
    };
    ASSERT_TRUE(memcmp(expected, payload, sizeof(expected)) == 0);

    net_repl_spawn_t decoded = {0};
    ASSERT_INT_EQ(NET_REPL_OK, net_repl_spawn_decode(&decoded, payload, sizeof(payload)));
    ASSERT_UINT_EQ(msg.entity_id, decoded.entity_id);
    ASSERT_UINT_EQ(msg.owner_client_id, decoded.owner_client_id);
    ASSERT_UINT_EQ(msg.join_time_u16, decoded.join_time_u16);
    ASSERT_INT_EQ(msg.pos_mm.x_mm, decoded.pos_mm.x_mm);
    ASSERT_INT_EQ(msg.pos_mm.y_mm, decoded.pos_mm.y_mm);
    ASSERT_INT_EQ(msg.pos_mm.z_mm, decoded.pos_mm.z_mm);
    return 0;
}

static int test_state_cube_encode_byte_layout_lockin(void) {
    /* Layout contract (big-endian):
     *  [0..1]  server_tick (u16)
     *  [2..3]  reserved (u16)
     *  [4..7]  entity_id (u32)
     *  [8..19] pos_mm xyz (i32,i32,i32)
     *  [20..27] quat snorm16 xyzw (i16,i16,i16,i16)
     */
    const net_repl_state_cube_t msg = {
        .server_tick = 0x00F0u,
        .entity_id = 0x01020304u,
        .pos_mm = {.x_mm = 1, .y_mm = 2, .z_mm = 3},
        .rot_snorm16 = {.x = 0, .y = 16384, .z = 0, .w = 16384},
    };

    uint8_t payload[NET_REPL_STATE_CUBE_PAYLOAD_SIZE];
    ASSERT_INT_EQ(NET_REPL_OK, net_repl_state_cube_encode(&msg, payload, sizeof(payload)));

    const uint8_t expected[NET_REPL_STATE_CUBE_PAYLOAD_SIZE] = {
        0x00u, 0xF0u,
        0x00u, 0x00u,
        0x01u, 0x02u, 0x03u, 0x04u,
        0x00u, 0x00u, 0x00u, 0x01u,
        0x00u, 0x00u, 0x00u, 0x02u,
        0x00u, 0x00u, 0x00u, 0x03u,
        0x00u, 0x00u,
        0x40u, 0x00u,
        0x00u, 0x00u,
        0x40u, 0x00u,
    };

    ASSERT_TRUE(memcmp(expected, payload, sizeof(expected)) == 0);

    net_repl_state_cube_t decoded = {0};
    ASSERT_INT_EQ(NET_REPL_OK, net_repl_state_cube_decode(&decoded, payload, sizeof(payload)));
    ASSERT_UINT_EQ(msg.server_tick, decoded.server_tick);
    ASSERT_UINT_EQ(msg.entity_id, decoded.entity_id);
    ASSERT_INT_EQ(msg.pos_mm.x_mm, decoded.pos_mm.x_mm);
    ASSERT_INT_EQ(msg.pos_mm.y_mm, decoded.pos_mm.y_mm);
    ASSERT_INT_EQ(msg.pos_mm.z_mm, decoded.pos_mm.z_mm);
    ASSERT_INT_EQ(msg.rot_snorm16.x, decoded.rot_snorm16.x);
    ASSERT_INT_EQ(msg.rot_snorm16.y, decoded.rot_snorm16.y);
    ASSERT_INT_EQ(msg.rot_snorm16.z, decoded.rot_snorm16.z);
    ASSERT_INT_EQ(msg.rot_snorm16.w, decoded.rot_snorm16.w);

    return 0;
}

static int test_welcome_encode_byte_layout_lockin(void) {
    /* Layout contract (big-endian):
     *  [0..1] expected_entities (u16)
     *  [2..3] tick_hz (u16)
     */
    const net_repl_welcome_t msg = {
        .expected_entities = 100u,
        .tick_hz = 60u,
    };

    uint8_t payload[NET_REPL_WELCOME_PAYLOAD_SIZE];
    ASSERT_INT_EQ(NET_REPL_OK, net_repl_welcome_encode(&msg, payload, sizeof(payload)));

    const uint8_t expected[NET_REPL_WELCOME_PAYLOAD_SIZE] = {
        0x00u, 0x64u, /* 100 */
        0x00u, 0x3Cu, /* 60 */
    };
    ASSERT_TRUE(memcmp(expected, payload, sizeof(expected)) == 0);

    net_repl_welcome_t decoded = {0};
    ASSERT_INT_EQ(NET_REPL_OK, net_repl_welcome_decode(&decoded, payload, sizeof(payload)));
    ASSERT_UINT_EQ(msg.expected_entities, decoded.expected_entities);
    ASSERT_UINT_EQ(msg.tick_hz, decoded.tick_hz);
    return 0;
}

static int test_spawn_batch_encode_decode_roundtrip(void) {
    net_repl_spawn_batch_entry_t entries[2] = {
        {.entity_id = 0x01020304u, .owner_client_id = 0x0001u, .pos_mm = {.x_mm = 1, .y_mm = 2, .z_mm = 3}},
        {.entity_id = 0xA1B2C3D4u, .owner_client_id = 0x0002u, .pos_mm = {.x_mm = 1000, .y_mm = -2000, .z_mm = 3000}},
    };

    uint8_t payload[256];
    size_t payload_size = 0u;
    ASSERT_INT_EQ(NET_REPL_OK,
                  net_repl_spawn_batch_encode(0x00F0u, entries, 2u, payload, sizeof(payload), &payload_size));

    const uint8_t expected[] = {
        0x00u, 0x02u, /* count */
        0x00u, 0xF0u, /* server_tick */

        0x01u, 0x02u, 0x03u, 0x04u,
        0x00u, 0x01u,
        0x00u, 0x00u, 0x00u, 0x01u,
        0x00u, 0x00u, 0x00u, 0x02u,
        0x00u, 0x00u, 0x00u, 0x03u,

        0xA1u, 0xB2u, 0xC3u, 0xD4u,
        0x00u, 0x02u,
        0x00u, 0x00u, 0x03u, 0xE8u, /* 1000 */
        0xFFu, 0xFFu, 0xF8u, 0x30u, /* -2000 */
        0x00u, 0x00u, 0x0Bu, 0xB8u, /* 3000 */
    };
    ASSERT_UINT_EQ(sizeof(expected), payload_size);
    ASSERT_TRUE(memcmp(expected, payload, payload_size) == 0);

    net_repl_spawn_batch_entry_t decoded_entries[2] = {0};
    uint16_t decoded_count = 0u;
    uint16_t decoded_tick = 0u;
    ASSERT_INT_EQ(NET_REPL_OK,
                  net_repl_spawn_batch_decode(&decoded_tick,
                                             decoded_entries,
                                             2u,
                                             &decoded_count,
                                             payload,
                                             payload_size));
    ASSERT_UINT_EQ(2u, decoded_count);
    ASSERT_UINT_EQ(0x00F0u, decoded_tick);
    ASSERT_UINT_EQ(entries[0].entity_id, decoded_entries[0].entity_id);
    ASSERT_UINT_EQ(entries[0].owner_client_id, decoded_entries[0].owner_client_id);
    ASSERT_INT_EQ(entries[0].pos_mm.x_mm, decoded_entries[0].pos_mm.x_mm);
    ASSERT_INT_EQ(entries[0].pos_mm.y_mm, decoded_entries[0].pos_mm.y_mm);
    ASSERT_INT_EQ(entries[0].pos_mm.z_mm, decoded_entries[0].pos_mm.z_mm);
    ASSERT_UINT_EQ(entries[1].entity_id, decoded_entries[1].entity_id);
    ASSERT_UINT_EQ(entries[1].owner_client_id, decoded_entries[1].owner_client_id);
    ASSERT_INT_EQ(entries[1].pos_mm.x_mm, decoded_entries[1].pos_mm.x_mm);
    ASSERT_INT_EQ(entries[1].pos_mm.y_mm, decoded_entries[1].pos_mm.y_mm);
    ASSERT_INT_EQ(entries[1].pos_mm.z_mm, decoded_entries[1].pos_mm.z_mm);
    return 0;
}

static int test_invalid_args_are_rejected(void) {
    uint8_t payload[NET_REPL_STATE_CUBE_PAYLOAD_SIZE] = {0};
    net_repl_state_cube_t decoded = {0};

    ASSERT_INT_EQ(NET_REPL_ERR_INVALID, net_repl_state_cube_decode(NULL, payload, sizeof(payload)));
    ASSERT_INT_EQ(NET_REPL_ERR_INVALID, net_repl_state_cube_decode(&decoded, NULL, sizeof(payload)));
    ASSERT_INT_EQ(NET_REPL_ERR_SHORT, net_repl_state_cube_decode(&decoded, payload, sizeof(payload) - 1u));

    return 0;
}

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    {"join_encode_byte_layout_lockin", test_join_encode_byte_layout_lockin},
    {"spawn_encode_byte_layout_lockin", test_spawn_encode_byte_layout_lockin},
    {"state_cube_encode_byte_layout_lockin", test_state_cube_encode_byte_layout_lockin},
    {"welcome_encode_byte_layout_lockin", test_welcome_encode_byte_layout_lockin},
    {"spawn_batch_encode_decode_roundtrip", test_spawn_batch_encode_decode_roundtrip},
    {"invalid_args_are_rejected", test_invalid_args_are_rejected},
};

int main(void) {
    size_t total = ARRAY_SIZE(TESTS);
    size_t passed = 0u;
    for (size_t i = 0u; i < total; ++i) {
        struct test_case *tc = &TESTS[i];
        printf("RUN %s\n", tc->name);
        int rc = tc->fn();
        if (rc == 0) {
            printf("OK %s\n", tc->name);
            passed++;
        } else {
            fprintf(stderr, "FAIL %s (rc=%d)\n", tc->name, rc);
            break;
        }
    }
    if (passed == total) {
        printf("All %zu tests passed\n", passed);
        return 0;
    }
    return 1;
}
