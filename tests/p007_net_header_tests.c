#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/net/packet_header.h"

#define ASSERT_TRUE(cond)                                                                                \
    do {                                                                                                 \
        if (!(cond)) {                                                                                   \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n", __FILE__, __LINE__, #cond);               \
            return 1;                                                                                    \
        }                                                                                                \
    } while (0)

#define ASSERT_INT_EQ(exp, act)                                                                          \
    do {                                                                                                 \
        if ((exp) != (act)) {                                                                            \
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

static int test_header_roundtrip(void) {
    net_packet_header_t header = {
        .protocol_id = 0x11223344u,
        .sequence = 0x1234u,
        .ack = 0x5678u,
        .ack_bits = 0x90ABCDEFu
    };
    uint8_t buffer[NET_PACKET_HEADER_SIZE];
    ASSERT_INT_EQ(NET_PACKET_HEADER_OK, net_packet_header_encode(&header, buffer, sizeof(buffer)));

    net_packet_header_t decoded = {0};
    ASSERT_INT_EQ(NET_PACKET_HEADER_OK, net_packet_header_decode(&decoded, buffer, sizeof(buffer)));
    ASSERT_UINT_EQ(header.protocol_id, decoded.protocol_id);
    ASSERT_UINT_EQ(header.sequence, decoded.sequence);
    ASSERT_UINT_EQ(header.ack, decoded.ack);
    ASSERT_UINT_EQ(header.ack_bits, decoded.ack_bits);
    return 0;
}

static int test_header_byte_order_layout(void) {
    net_packet_header_t header = {
        .protocol_id = 0x01020304u,
        .sequence = 0x0506u,
        .ack = 0x0708u,
        .ack_bits = 0x0A0B0C0Du
    };
    uint8_t buffer[NET_PACKET_HEADER_SIZE];
    ASSERT_INT_EQ(NET_PACKET_HEADER_OK, net_packet_header_encode(&header, buffer, sizeof(buffer)));

    const uint8_t expected[] = {
        0x01u, 0x02u, 0x03u, 0x04u,
        0x05u, 0x06u,
        0x07u, 0x08u,
        0x0Au, 0x0Bu, 0x0Cu, 0x0Du
    };
    ASSERT_TRUE(sizeof(expected) == NET_PACKET_HEADER_SIZE);
    ASSERT_TRUE(memcmp(expected, buffer, NET_PACKET_HEADER_SIZE) == 0);
    return 0;
}

static int test_encode_rejects_short_buffer(void) {
    net_packet_header_t header = {
        .protocol_id = 0xAABBCCDDu,
        .sequence = 0x0102u,
        .ack = 0x0304u,
        .ack_bits = 0x05060708u
    };
    uint8_t buffer[NET_PACKET_HEADER_SIZE - 1u];
    memset(buffer, 0xCD, sizeof(buffer));
    ASSERT_INT_EQ(NET_PACKET_HEADER_ERR_SHORT, net_packet_header_encode(&header, buffer, sizeof(buffer)));
    return 0;
}

static int test_decode_rejects_short_buffer(void) {
    uint8_t buffer[NET_PACKET_HEADER_SIZE - 1u];
    memset(buffer, 0xAB, sizeof(buffer));

    net_packet_header_t decoded = {
        .protocol_id = 1u,
        .sequence = 2u,
        .ack = 3u,
        .ack_bits = 4u
    };

    ASSERT_INT_EQ(NET_PACKET_HEADER_ERR_SHORT, net_packet_header_decode(&decoded, buffer, sizeof(buffer)));
    ASSERT_UINT_EQ(1u, decoded.protocol_id);
    ASSERT_UINT_EQ(2u, decoded.sequence);
    ASSERT_UINT_EQ(3u, decoded.ack);
    ASSERT_UINT_EQ(4u, decoded.ack_bits);
    return 0;
}

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    {"header_roundtrip", test_header_roundtrip},
    {"header_byte_order_layout", test_header_byte_order_layout},
    {"encode_rejects_short_buffer", test_encode_rejects_short_buffer},
    {"decode_rejects_short_buffer", test_decode_rejects_short_buffer},
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
