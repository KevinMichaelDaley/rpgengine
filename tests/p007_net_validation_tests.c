/**
 * @file p007_net_validation_tests.c
 * @brief RED tests for protocol validation and instrumentation counters.
 *
 * Covers:
 *   1. Valid packet passes validation, counters increment
 *   2. Protocol ID mismatch increments protocol_errors
 *   3. Unknown schema ID increments unknown_schema count
 *   4. Truncated packet (too short) increments truncated count
 *   5. Payload size mismatch increments malformed count
 *   6. Counter reset clears all stats
 *   7. Multiple errors accumulate correctly
 *   8. NULL safety
 */

#include <stdio.h>
#include <string.h>

#include "ferrum/net/validation.h"

/* ── Minimal test harness ───────────────────────────────────────── */

static int g_pass_count;
static int g_fail_count;

#define TEST(name) static void name(void)
#define RUN(name) do { \
    printf("  %-52s ", #name); \
    name(); \
    printf("PASS\n"); \
    g_pass_count++; \
} while (0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("FAIL\n    %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        g_fail_count++; return; \
    } \
} while (0)

#define ASSERT_EQ(a, b) ASSERT((a) == (b))

/* ── Helpers ────────────────────────────────────────────────────── */

/** Write a uint32 big-endian at buf. */
static void write_u32_be(uint8_t *buf, uint32_t v) {
    buf[0] = (uint8_t)(v >> 24);
    buf[1] = (uint8_t)(v >> 16);
    buf[2] = (uint8_t)(v >> 8);
    buf[3] = (uint8_t)(v);
}

/** Write a uint16 big-endian at buf. */
static void write_u16_be(uint8_t *buf, uint16_t v) {
    buf[0] = (uint8_t)(v >> 8);
    buf[1] = (uint8_t)(v);
}

/**
 * Build a minimal valid packet:
 * [protocol_id:4][seq:2][ack:2][ack_bits:4] = 12 bytes header
 * + [schema_id:2][payload_size:2][payload...] = 4 + payload_size
 */
static size_t build_packet(uint8_t *buf, size_t cap,
                           uint32_t protocol_id,
                           uint16_t schema_id,
                           const uint8_t *payload,
                           uint16_t payload_size) {
    (void)cap;
    /* Packet header (12 bytes). */
    write_u32_be(buf + 0, protocol_id);
    write_u16_be(buf + 4, 1);   /* sequence */
    write_u16_be(buf + 6, 0);   /* ack */
    write_u32_be(buf + 8, 0);   /* ack_bits */

    /* Schema header (4 bytes). */
    write_u16_be(buf + 12, schema_id);
    write_u16_be(buf + 14, payload_size);

    /* Payload. */
    if (payload && payload_size > 0) {
        memcpy(buf + 16, payload, payload_size);
    }

    return 16 + payload_size;
}

/* ── Tests ──────────────────────────────────────────────────────── */

/**
 * A valid packet passes validation and increments packets_valid.
 */
TEST(test_valid_packet) {
    net_validation_ctx_t ctx;
    uint16_t known_schemas[] = {0x2001, 0x200B};
    net_validation_init(&ctx, 0x52555038u, known_schemas, 2);

    uint8_t payload[4] = {0x01, 0x02, 0x03, 0x04};
    uint8_t pkt[64];
    size_t len = build_packet(pkt, sizeof(pkt), 0x52555038u,
                              0x2001, payload, 4);

    int rc = net_validation_check(&ctx, pkt, len);
    ASSERT_EQ(rc, NET_VALIDATION_OK);
    ASSERT_EQ(ctx.stats.packets_valid, 1);
    ASSERT_EQ(ctx.stats.packets_total, 1);
    ASSERT_EQ(ctx.stats.bytes_total, len);
}

/**
 * Protocol ID mismatch → dropped, counter incremented.
 */
TEST(test_protocol_id_mismatch) {
    net_validation_ctx_t ctx;
    uint16_t known[] = {0x2001};
    net_validation_init(&ctx, 0x52555038u, known, 1);

    uint8_t payload[4] = {0};
    uint8_t pkt[64];
    size_t len = build_packet(pkt, sizeof(pkt), 0xDEADBEEF,
                              0x2001, payload, 4);

    int rc = net_validation_check(&ctx, pkt, len);
    ASSERT_EQ(rc, NET_VALIDATION_ERR_PROTOCOL);
    ASSERT_EQ(ctx.stats.protocol_errors, 1);
    ASSERT_EQ(ctx.stats.packets_valid, 0);
    ASSERT_EQ(ctx.stats.packets_total, 1);
}

/**
 * Unknown schema ID → dropped, counter incremented.
 */
TEST(test_unknown_schema) {
    net_validation_ctx_t ctx;
    uint16_t known[] = {0x2001};
    net_validation_init(&ctx, 0x52555038u, known, 1);

    uint8_t payload[4] = {0};
    uint8_t pkt[64];
    size_t len = build_packet(pkt, sizeof(pkt), 0x52555038u,
                              0xFFFF, payload, 4);

    int rc = net_validation_check(&ctx, pkt, len);
    ASSERT_EQ(rc, NET_VALIDATION_ERR_SCHEMA);
    ASSERT_EQ(ctx.stats.unknown_schemas, 1);
}

/**
 * Truncated packet (less than header size) → dropped.
 */
TEST(test_truncated_packet) {
    net_validation_ctx_t ctx;
    net_validation_init(&ctx, 0x52555038u, NULL, 0);

    uint8_t pkt[8] = {0}; /* too short for 12-byte header */

    int rc = net_validation_check(&ctx, pkt, 8);
    ASSERT_EQ(rc, NET_VALIDATION_ERR_TRUNCATED);
    ASSERT_EQ(ctx.stats.truncated_packets, 1);
}

/**
 * Payload size field exceeds remaining bytes → malformed.
 */
TEST(test_payload_size_mismatch) {
    net_validation_ctx_t ctx;
    uint16_t known[] = {0x2001};
    net_validation_init(&ctx, 0x52555038u, known, 1);

    uint8_t pkt[64];
    /* Build header with protocol OK. */
    write_u32_be(pkt + 0, 0x52555038u);
    write_u16_be(pkt + 4, 1);
    write_u16_be(pkt + 6, 0);
    write_u32_be(pkt + 8, 0);
    /* Schema header claims 100 bytes of payload but packet is only 20. */
    write_u16_be(pkt + 12, 0x2001);
    write_u16_be(pkt + 14, 100);

    int rc = net_validation_check(&ctx, pkt, 20);
    ASSERT_EQ(rc, NET_VALIDATION_ERR_MALFORMED);
    ASSERT_EQ(ctx.stats.malformed_packets, 1);
}

/**
 * Reset clears all counters.
 */
TEST(test_counter_reset) {
    net_validation_ctx_t ctx;
    net_validation_init(&ctx, 0x52555038u, NULL, 0);

    /* Generate some errors. */
    uint8_t pkt[4] = {0};
    net_validation_check(&ctx, pkt, 4);
    net_validation_check(&ctx, pkt, 4);

    ASSERT_EQ(ctx.stats.packets_total, 2);

    net_validation_reset_stats(&ctx);
    ASSERT_EQ(ctx.stats.packets_total, 0);
    ASSERT_EQ(ctx.stats.truncated_packets, 0);
    ASSERT_EQ(ctx.stats.protocol_errors, 0);
}

/**
 * Multiple mixed errors accumulate correctly.
 */
TEST(test_multiple_errors_accumulate) {
    net_validation_ctx_t ctx;
    uint16_t known[] = {0x2001};
    net_validation_init(&ctx, 0x52555038u, known, 1);

    /* 2 truncated. */
    uint8_t short_pkt[4] = {0};
    net_validation_check(&ctx, short_pkt, 4);
    net_validation_check(&ctx, short_pkt, 4);

    /* 1 bad protocol. */
    uint8_t payload[4] = {0};
    uint8_t pkt[64];
    size_t len = build_packet(pkt, sizeof(pkt), 0xBAD00000,
                              0x2001, payload, 4);
    net_validation_check(&ctx, pkt, len);

    /* 1 valid. */
    len = build_packet(pkt, sizeof(pkt), 0x52555038u,
                       0x2001, payload, 4);
    net_validation_check(&ctx, pkt, len);

    ASSERT_EQ(ctx.stats.packets_total, 4);
    ASSERT_EQ(ctx.stats.truncated_packets, 2);
    ASSERT_EQ(ctx.stats.protocol_errors, 1);
    ASSERT_EQ(ctx.stats.packets_valid, 1);
}

/**
 * NULL safety.
 */
TEST(test_null_safety) {
    net_validation_init(NULL, 0, NULL, 0); /* no-op */

    net_validation_ctx_t ctx;
    net_validation_init(&ctx, 0, NULL, 0);

    ASSERT_EQ(net_validation_check(NULL, NULL, 0),
              NET_VALIDATION_ERR_INVALID);
    ASSERT_EQ(net_validation_check(&ctx, NULL, 10),
              NET_VALIDATION_ERR_INVALID);

    net_validation_reset_stats(NULL); /* no-op */
}

/* ── Runner ─────────────────────────────────────────────────────── */

int main(void) {
    printf("p007_net_validation_tests:\n");
    RUN(test_valid_packet);
    RUN(test_protocol_id_mismatch);
    RUN(test_unknown_schema);
    RUN(test_truncated_packet);
    RUN(test_payload_size_mismatch);
    RUN(test_counter_reset);
    RUN(test_multiple_errors_accumulate);
    RUN(test_null_safety);
    printf("%d/%d tests passed\n", g_pass_count,
           g_pass_count + g_fail_count);
    return g_fail_count ? 1 : 0;
}
