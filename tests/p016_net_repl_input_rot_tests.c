#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/net/replication/input_rot.h"

#define ASSERT_TRUE(cond)                                                                                \
    do {                                                                                                 \
        if (!(cond)) {                                                                                   \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
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

#define ASSERT_U32_EQ(exp, act)                                                                          \
    do {                                                                                                 \
        if ((uint32_t)(exp) != (uint32_t)(act)) {                                                        \
            fprintf(stderr, "ASSERT_U32_EQ failed: %s:%d: expected %u got %u\n", __FILE__, __LINE__,     \
                    (unsigned)(exp), (unsigned)(act));                                                   \
            return 1;                                                                                    \
        }                                                                                                \
    } while (0)

#define ASSERT_I16_EQ(exp, act)                                                                          \
    do {                                                                                                 \
        if ((int16_t)(exp) != (int16_t)(act)) {                                                          \
            fprintf(stderr, "ASSERT_I16_EQ failed: %s:%d: expected %d got %d\n", __FILE__, __LINE__,     \
                    (int)(int16_t)(exp), (int)(int16_t)(act));                                           \
            return 1;                                                                                    \
        }                                                                                                \
    } while (0)

#define ASSERT_U16_EQ(exp, act)                                                                          \
    do {                                                                                                 \
        if ((uint16_t)(exp) != (uint16_t)(act)) {                                                        \
            fprintf(stderr, "ASSERT_U16_EQ failed: %s:%d: expected %u got %u\n", __FILE__, __LINE__,     \
                    (unsigned)(uint16_t)(exp), (unsigned)(uint16_t)(act));                               \
            return 1;                                                                                    \
        }                                                                                                \
    } while (0)

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static int test_input_rot_encode_byte_layout_lockin(void) {
    /* Layout contract (big-endian):
     *  [0..3]  entity_id (u32)
     *  [4..7]  event_id (u32)
     *  [8..13] axis snorm16 xyz (i16,i16,i16)
     *  [14..15] speed_millirad_per_s (u16)
     */
    const net_repl_input_rot_t msg = {
        .entity_id = 0x01020304u,
        .event_id = 0xAABBCCDDu,
        .axis_x_snorm16 = 0,
        .axis_y_snorm16 = 0,
        .axis_z_snorm16 = 32767,
        .speed_millirad_per_s = 1500u,
    };

    uint8_t payload[NET_REPL_INPUT_ROT_PAYLOAD_SIZE];
    ASSERT_INT_EQ(NET_REPL_OK, net_repl_input_rot_encode(&msg, payload, sizeof(payload)));

    const uint8_t expected[NET_REPL_INPUT_ROT_PAYLOAD_SIZE] = {
        0x01u, 0x02u, 0x03u, 0x04u,
        0xAAu, 0xBBu, 0xCCu, 0xDDu,
        0x00u, 0x00u,
        0x00u, 0x00u,
        0x7Fu, 0xFFu,
        0x05u, 0xDCu, /* 1500 */
    };
    ASSERT_TRUE(memcmp(expected, payload, sizeof(expected)) == 0);

    net_repl_input_rot_t decoded;
    memset(&decoded, 0, sizeof(decoded));
    ASSERT_INT_EQ(NET_REPL_OK, net_repl_input_rot_decode(&decoded, payload, sizeof(payload)));
    ASSERT_U32_EQ(msg.entity_id, decoded.entity_id);
    ASSERT_U32_EQ(msg.event_id, decoded.event_id);
    ASSERT_I16_EQ(msg.axis_x_snorm16, decoded.axis_x_snorm16);
    ASSERT_I16_EQ(msg.axis_y_snorm16, decoded.axis_y_snorm16);
    ASSERT_I16_EQ(msg.axis_z_snorm16, decoded.axis_z_snorm16);
    ASSERT_U16_EQ(msg.speed_millirad_per_s, decoded.speed_millirad_per_s);
    return 0;
}

static int test_input_rot_invalid_args_rejected(void) {
    uint8_t payload[NET_REPL_INPUT_ROT_PAYLOAD_SIZE] = {0};
    net_repl_input_rot_t decoded;
    memset(&decoded, 0, sizeof(decoded));

    ASSERT_INT_EQ(NET_REPL_ERR_INVALID, net_repl_input_rot_encode(NULL, payload, sizeof(payload)));
    ASSERT_INT_EQ(NET_REPL_ERR_INVALID, net_repl_input_rot_encode(&decoded, NULL, sizeof(payload)));
    ASSERT_INT_EQ(NET_REPL_ERR_SHORT, net_repl_input_rot_encode(&decoded, payload, sizeof(payload) - 1u));

    ASSERT_INT_EQ(NET_REPL_ERR_INVALID, net_repl_input_rot_decode(NULL, payload, sizeof(payload)));
    ASSERT_INT_EQ(NET_REPL_ERR_INVALID, net_repl_input_rot_decode(&decoded, NULL, sizeof(payload)));
    ASSERT_INT_EQ(NET_REPL_ERR_SHORT, net_repl_input_rot_decode(&decoded, payload, sizeof(payload) - 1u));
    return 0;
}

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    {"input_rot_encode_byte_layout_lockin", test_input_rot_encode_byte_layout_lockin},
    {"input_rot_invalid_args_rejected", test_input_rot_invalid_args_rejected},
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
