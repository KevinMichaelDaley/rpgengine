#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/server/net/inbound_message.h"

#define TEST_FAIL(msg, ...)                                                                               \
    do {                                                                                                  \
        fprintf(stderr, "FAIL %s:%d: " msg "\n", __FILE__, __LINE__, ##__VA_ARGS__);                    \
        return 1;                                                                                         \
    } while (0)

#define ASSERT_TRUE(cond)                                                                                 \
    do {                                                                                                  \
        if (!(cond)) {                                                                                    \
            TEST_FAIL("%s", #cond);                                                                      \
        }                                                                                                 \
    } while (0)

#define ASSERT_U16_EQ(exp, act)                                                                           \
    do {                                                                                                  \
        uint16_t _e = (uint16_t)(exp);                                                                    \
        uint16_t _a = (uint16_t)(act);                                                                    \
        if (_e != _a) {                                                                                   \
            TEST_FAIL("expected %u got %u", (unsigned)_e, (unsigned)_a);                                 \
        }                                                                                                 \
    } while (0)

#define ASSERT_SIZE_EQ(exp, act)                                                                          \
    do {                                                                                                  \
        size_t _e = (size_t)(exp);                                                                        \
        size_t _a = (size_t)(act);                                                                        \
        if (_e != _a) {                                                                                   \
            TEST_FAIL("expected %zu got %zu", _e, _a);                                                   \
        }                                                                                                 \
    } while (0)

static int test_encode_decode_roundtrip(void) {
    uint8_t payload[5] = {1u, 2u, 3u, 4u, 5u};

    uint8_t msg[64];
    size_t msg_size = 0u;
    ASSERT_TRUE(fr_server_net_inbound_message_encode(0x1234u,
                                                     true,
                                                     0xBEEFu,
                                                     payload,
                                                     sizeof(payload),
                                                     msg,
                                                     sizeof(msg),
                                                     &msg_size));

    fr_server_net_inbound_message_view_t view;
    memset(&view, 0, sizeof(view));
    ASSERT_TRUE(fr_server_net_inbound_message_decode(&view, msg, msg_size));

    ASSERT_U16_EQ(0x1234u, view.client_id);
    ASSERT_U16_EQ(0xBEEFu, view.schema_id);
    ASSERT_TRUE(view.reliable);
    ASSERT_SIZE_EQ(sizeof(payload), view.payload_size);
    ASSERT_TRUE(view.payload != NULL);
    ASSERT_TRUE(memcmp(view.payload, payload, sizeof(payload)) == 0);
    return 0;
}

static int test_decode_rejects_short_buffer(void) {
    uint8_t msg[5] = {0};
    fr_server_net_inbound_message_view_t view;
    memset(&view, 0, sizeof(view));
    ASSERT_TRUE(!fr_server_net_inbound_message_decode(&view, msg, sizeof(msg)));
    return 0;
}

static int test_encode_rejects_too_small_output(void) {
    uint8_t payload[3] = {9u, 8u, 7u};
    uint8_t msg[8];
    size_t msg_size = 0u;

    /* Needs 6 + payload bytes; 8 is too small for payload len 3. */
    ASSERT_TRUE(!fr_server_net_inbound_message_encode(1u, false, 2u, payload, sizeof(payload), msg, sizeof(msg), &msg_size));
    return 0;
}

int main(void) {
    struct {
        const char *name;
        int (*fn)(void);
    } tests[] = {
        {"encode_decode_roundtrip", test_encode_decode_roundtrip},
        {"decode_rejects_short_buffer", test_decode_rejects_short_buffer},
        {"encode_rejects_too_small_output", test_encode_rejects_too_small_output},
    };

    for (size_t i = 0u; i < (sizeof(tests) / sizeof(tests[0])); ++i) {
        fprintf(stderr, "RUN %s\n", tests[i].name);
        int rc = tests[i].fn();
        if (rc != 0) {
            return rc;
        }
        fprintf(stderr, "OK %s\n", tests[i].name);
    }

    fprintf(stderr, "All %zu tests passed\n", (sizeof(tests) / sizeof(tests[0])));
    return 0;
}
