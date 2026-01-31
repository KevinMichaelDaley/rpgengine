#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/net/bit_pack.h"
#include "ferrum/net/schema_registry.h"

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

static int test_bit_pack_encode_byte_layout_lockin(void) {
    /* Layout contract:
     *  [0..1] schema_id (u16 big-endian)
     *  [2..3] payload_size (u16 big-endian)
     *  [4..]  payload bytes verbatim
     */
    const uint8_t payload[] = {0xAAu, 0xBBu, 0xCCu};
    uint8_t bytes[64];

    net_bit_pack_header_t header = {
        .schema_id = 0x0102u,
        .payload_size = (uint16_t)sizeof(payload),
    };

    size_t written = 0u;
    ASSERT_INT_EQ(NET_BIT_PACK_OK,
                  net_bit_pack_encode(&header, payload, sizeof(payload), bytes, sizeof(bytes), &written));

    const uint8_t expected[] = {0x01u, 0x02u, 0x00u, 0x03u, 0xAAu, 0xBBu, 0xCCu};
    ASSERT_UINT_EQ(sizeof(expected), written);
    ASSERT_TRUE(memcmp(expected, bytes, sizeof(expected)) == 0);
    return 0;
}

static int test_bit_pack_decode_rejects_truncated_header(void) {
    const uint8_t bytes[] = {0x01u, 0x02u, 0x00u};

    net_bit_pack_header_t header = {0};
    const uint8_t *payload = NULL;
    size_t payload_size = 0u;

    ASSERT_INT_EQ(NET_BIT_PACK_ERR_SHORT,
                  net_bit_pack_decode(&header, bytes, sizeof(bytes), &payload, &payload_size));
    return 0;
}

static int test_bit_pack_decode_rejects_length_longer_than_buffer(void) {
    /* Header says payload_size=5, but only 3 bytes available. */
    const uint8_t bytes[] = {
        0x12u, 0x34u,
        0x00u, 0x05u,
        0xDEu, 0xADu, 0xBEu,
    };

    net_bit_pack_header_t header = {0};
    const uint8_t *payload = NULL;
    size_t payload_size = 0u;

    ASSERT_INT_EQ(NET_BIT_PACK_ERR_MALFORMED,
                  net_bit_pack_decode(&header, bytes, sizeof(bytes), &payload, &payload_size));
    return 0;
}

static int test_schema_registry_rejects_unknown_schema_id(void) {
    net_schema_registry_t registry;
    net_schema_registry_init(&registry);

    ASSERT_INT_EQ(NET_SCHEMA_REGISTRY_OK, net_schema_registry_register(&registry, 0x0102u, 3u));

    const uint8_t bytes[] = {
        0x99u, 0x99u,
        0x00u, 0x00u,
    };

    uint16_t schema_id = 0u;
    const uint8_t *payload = NULL;
    size_t payload_size = 0u;

    ASSERT_INT_EQ(NET_SCHEMA_REGISTRY_ERR_UNKNOWN_SCHEMA,
                  net_schema_registry_decode_packet(&registry, bytes, sizeof(bytes), &schema_id, &payload, &payload_size));
    return 0;
}

static int test_schema_registry_rejects_payload_length_mismatch(void) {
    net_schema_registry_t registry;
    net_schema_registry_init(&registry);

    /* Expect schema 0x0102 payload to be 3 bytes, but packet says 2. */
    ASSERT_INT_EQ(NET_SCHEMA_REGISTRY_OK, net_schema_registry_register(&registry, 0x0102u, 3u));

    const uint8_t bytes[] = {
        0x01u, 0x02u,
        0x00u, 0x02u,
        0xAAu, 0xBBu,
    };

    uint16_t schema_id = 0u;
    const uint8_t *payload = NULL;
    size_t payload_size = 0u;

    ASSERT_INT_EQ(NET_SCHEMA_REGISTRY_ERR_PAYLOAD_LENGTH,
                  net_schema_registry_decode_packet(&registry, bytes, sizeof(bytes), &schema_id, &payload, &payload_size));
    return 0;
}

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    {"bit_pack_encode_byte_layout_lockin", test_bit_pack_encode_byte_layout_lockin},
    {"bit_pack_decode_rejects_truncated_header", test_bit_pack_decode_rejects_truncated_header},
    {"bit_pack_decode_rejects_length_longer_than_buffer", test_bit_pack_decode_rejects_length_longer_than_buffer},
    {"schema_registry_rejects_unknown_schema_id", test_schema_registry_rejects_unknown_schema_id},
    {"schema_registry_rejects_payload_length_mismatch", test_schema_registry_rejects_payload_length_mismatch},
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
