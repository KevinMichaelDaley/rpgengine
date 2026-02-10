/**
 * @file p007_net_snapshot_chunk_tests.c
 * @brief RED tests for snapshot chunking and reassembly.
 *
 * Covers:
 *   1. Small payload fits in a single chunk
 *   2. Large payload splits into multiple chunks with headers
 *   3. Reassembly produces original payload after all chunks arrive
 *   4. Out-of-order chunk arrival still reassembles correctly
 *   5. Duplicate chunks are ignored
 *   6. Incomplete reassembly returns not-ready
 *   7. Chunk capacity overflow returns error
 *   8. Reset clears reassembly state
 *   9. NULL safety
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/net/snapshot_chunk.h"

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

/** Fill a buffer with a deterministic pattern. */
static void fill_pattern(uint8_t *buf, size_t len) {
    for (size_t i = 0; i < len; i++) {
        buf[i] = (uint8_t)(i & 0xFF);
    }
}

/* ── Tests ──────────────────────────────────────────────────────── */

/**
 * A payload smaller than chunk_size fits in exactly one chunk.
 */
TEST(test_single_chunk) {
    uint8_t payload[100];
    fill_pattern(payload, sizeof(payload));

    /* Split into chunks of 256 bytes → should produce 1 chunk. */
    net_chunk_header_t headers[4];
    uint32_t chunk_count = 0;

    int rc = net_snapshot_chunk_split(payload, sizeof(payload), 256,
                                     headers, 4, &chunk_count);
    ASSERT_EQ(rc, NET_CHUNK_OK);
    ASSERT_EQ(chunk_count, 1);
    ASSERT_EQ(headers[0].chunk_index, 0);
    ASSERT_EQ(headers[0].chunk_total, 1);
    ASSERT_EQ(headers[0].offset, 0);
    ASSERT_EQ(headers[0].length, 100);
}

/**
 * A payload larger than chunk_size is split into multiple chunks.
 * Each chunk header has correct offset, length, and total count.
 */
TEST(test_multi_chunk_split) {
    uint8_t payload[1000];
    fill_pattern(payload, sizeof(payload));

    /* Chunk size 300 → ceil(1000/300) = 4 chunks. */
    net_chunk_header_t headers[8];
    uint32_t chunk_count = 0;

    int rc = net_snapshot_chunk_split(payload, sizeof(payload), 300,
                                     headers, 8, &chunk_count);
    ASSERT_EQ(rc, NET_CHUNK_OK);
    ASSERT_EQ(chunk_count, 4);

    /* Verify chunk offsets and lengths. */
    ASSERT_EQ(headers[0].offset, 0);
    ASSERT_EQ(headers[0].length, 300);
    ASSERT_EQ(headers[0].chunk_index, 0);
    ASSERT_EQ(headers[0].chunk_total, 4);

    ASSERT_EQ(headers[1].offset, 300);
    ASSERT_EQ(headers[1].length, 300);

    ASSERT_EQ(headers[2].offset, 600);
    ASSERT_EQ(headers[2].length, 300);

    /* Last chunk is the remainder. */
    ASSERT_EQ(headers[3].offset, 900);
    ASSERT_EQ(headers[3].length, 100);
    ASSERT_EQ(headers[3].chunk_index, 3);
    ASSERT_EQ(headers[3].chunk_total, 4);
}

/**
 * Feed all chunks into the reassembler in order → produces
 * the original payload byte-for-byte.
 */
TEST(test_reassembly_in_order) {
    uint8_t payload[500];
    fill_pattern(payload, sizeof(payload));

    /* Split into 200-byte chunks → 3 chunks. */
    net_chunk_header_t headers[4];
    uint32_t chunk_count = 0;
    net_snapshot_chunk_split(payload, sizeof(payload), 200,
                            headers, 4, &chunk_count);
    ASSERT_EQ(chunk_count, 3);

    /* Set up reassembler. */
    uint8_t reasm_buf[512];
    net_chunk_reassembly_t reasm;
    net_chunk_reassembly_init(&reasm, reasm_buf, sizeof(reasm_buf));

    /* Feed chunks in order. */
    int ready;
    for (uint32_t i = 0; i < chunk_count; i++) {
        const uint8_t *chunk_data = payload + headers[i].offset;
        ready = net_chunk_reassembly_push(&reasm, &headers[i],
                                          chunk_data, headers[i].length);
        if (i < chunk_count - 1) {
            ASSERT_EQ(ready, NET_CHUNK_NOT_READY);
        }
    }
    ASSERT_EQ(ready, NET_CHUNK_READY);

    /* Verify reassembled payload matches original. */
    ASSERT_EQ(reasm.total_size, sizeof(payload));
    ASSERT(memcmp(reasm.buffer, payload, sizeof(payload)) == 0);
}

/**
 * Chunks arriving out of order (last, first, middle) still
 * reassemble correctly.
 */
TEST(test_reassembly_out_of_order) {
    uint8_t payload[600];
    fill_pattern(payload, sizeof(payload));

    net_chunk_header_t headers[4];
    uint32_t chunk_count = 0;
    net_snapshot_chunk_split(payload, sizeof(payload), 200,
                            headers, 4, &chunk_count);
    ASSERT_EQ(chunk_count, 3);

    uint8_t reasm_buf[1024];
    net_chunk_reassembly_t reasm;
    net_chunk_reassembly_init(&reasm, reasm_buf, sizeof(reasm_buf));

    /* Feed in order: 2, 0, 1. */
    int order[] = {2, 0, 1};
    int ready = NET_CHUNK_NOT_READY;
    for (int i = 0; i < 3; i++) {
        int idx = order[i];
        const uint8_t *chunk_data = payload + headers[idx].offset;
        ready = net_chunk_reassembly_push(&reasm, &headers[idx],
                                          chunk_data, headers[idx].length);
    }
    ASSERT_EQ(ready, NET_CHUNK_READY);
    ASSERT_EQ(reasm.total_size, sizeof(payload));
    ASSERT(memcmp(reasm.buffer, payload, sizeof(payload)) == 0);
}

/**
 * Pushing the same chunk twice is harmlessly ignored.
 */
TEST(test_duplicate_chunk_ignored) {
    uint8_t payload[400];
    fill_pattern(payload, sizeof(payload));

    net_chunk_header_t headers[4];
    uint32_t chunk_count = 0;
    net_snapshot_chunk_split(payload, sizeof(payload), 200,
                            headers, 4, &chunk_count);
    ASSERT_EQ(chunk_count, 2);

    uint8_t reasm_buf[512];
    net_chunk_reassembly_t reasm;
    net_chunk_reassembly_init(&reasm, reasm_buf, sizeof(reasm_buf));

    /* Push chunk 0 twice. */
    int r0 = net_chunk_reassembly_push(&reasm, &headers[0],
                                       payload + 0, 200);
    ASSERT_EQ(r0, NET_CHUNK_NOT_READY);

    int r0_dup = net_chunk_reassembly_push(&reasm, &headers[0],
                                           payload + 0, 200);
    ASSERT_EQ(r0_dup, NET_CHUNK_NOT_READY);

    /* Push chunk 1 → ready. */
    int r1 = net_chunk_reassembly_push(&reasm, &headers[1],
                                       payload + 200, 200);
    ASSERT_EQ(r1, NET_CHUNK_READY);
    ASSERT(memcmp(reasm.buffer, payload, sizeof(payload)) == 0);
}

/**
 * Incomplete reassembly (not all chunks pushed) stays not-ready.
 */
TEST(test_incomplete_not_ready) {
    uint8_t payload[600];
    fill_pattern(payload, sizeof(payload));

    net_chunk_header_t headers[4];
    uint32_t chunk_count = 0;
    net_snapshot_chunk_split(payload, sizeof(payload), 200,
                            headers, 4, &chunk_count);
    ASSERT_EQ(chunk_count, 3);

    uint8_t reasm_buf[1024];
    net_chunk_reassembly_t reasm;
    net_chunk_reassembly_init(&reasm, reasm_buf, sizeof(reasm_buf));

    /* Push only chunk 0 and 2 — missing chunk 1. */
    int r = net_chunk_reassembly_push(&reasm, &headers[0],
                                      payload + 0, 200);
    ASSERT_EQ(r, NET_CHUNK_NOT_READY);

    r = net_chunk_reassembly_push(&reasm, &headers[2],
                                  payload + 400, 200);
    ASSERT_EQ(r, NET_CHUNK_NOT_READY);

    ASSERT_EQ(reasm.chunks_received, 2);
}

/**
 * Header array too small returns NET_CHUNK_ERR_CAPACITY.
 */
TEST(test_split_capacity_overflow) {
    uint8_t payload[1000];
    memset(payload, 0, sizeof(payload));
    net_chunk_header_t headers[2]; /* too small for 4 chunks */
    uint32_t chunk_count = 0;

    int rc = net_snapshot_chunk_split(payload, sizeof(payload), 300,
                                     headers, 2, &chunk_count);
    ASSERT_EQ(rc, NET_CHUNK_ERR_CAPACITY);
}

/**
 * Reset clears all reassembly state.
 */
TEST(test_reset_clears_state) {
    uint8_t reasm_buf[256];
    net_chunk_reassembly_t reasm;
    net_chunk_reassembly_init(&reasm, reasm_buf, sizeof(reasm_buf));

    /* Push a chunk to dirty the state. */
    net_chunk_header_t hdr = {
        .chunk_index = 0, .chunk_total = 2,
        .offset = 0, .length = 50
    };
    uint8_t data[50];
    memset(data, 0xAB, sizeof(data));
    net_chunk_reassembly_push(&reasm, &hdr, data, 50);

    ASSERT_EQ(reasm.chunks_received, 1);

    net_chunk_reassembly_reset(&reasm);
    ASSERT_EQ(reasm.chunks_received, 0);
    ASSERT_EQ(reasm.chunks_expected, 0);
    ASSERT_EQ(reasm.total_size, 0);
    ASSERT_EQ(reasm.received_mask, 0);
}

/**
 * NULL arguments return error codes or are no-ops.
 */
TEST(test_null_safety) {
    net_chunk_header_t headers[4];
    uint32_t chunk_count = 0;
    uint8_t buf[100];

    ASSERT_EQ(net_snapshot_chunk_split(NULL, 100, 50, headers, 4,
                                       &chunk_count),
              NET_CHUNK_ERR_INVALID);
    ASSERT_EQ(net_snapshot_chunk_split(buf, 100, 50, NULL, 4,
                                       &chunk_count),
              NET_CHUNK_ERR_INVALID);

    net_chunk_reassembly_t reasm;
    net_chunk_reassembly_init(NULL, buf, sizeof(buf)); /* no-op */
    net_chunk_reassembly_init(&reasm, buf, sizeof(buf));

    net_chunk_header_t hdr = {0, 1, 0, 10};
    ASSERT_EQ(net_chunk_reassembly_push(NULL, &hdr, buf, 10),
              NET_CHUNK_ERR_INVALID);
    ASSERT_EQ(net_chunk_reassembly_push(&reasm, NULL, buf, 10),
              NET_CHUNK_ERR_INVALID);

    net_chunk_reassembly_reset(NULL); /* no-op */
}

/* ── Runner ─────────────────────────────────────────────────────── */

int main(void) {
    printf("p007_net_snapshot_chunk_tests:\n");
    RUN(test_single_chunk);
    RUN(test_multi_chunk_split);
    RUN(test_reassembly_in_order);
    RUN(test_reassembly_out_of_order);
    RUN(test_duplicate_chunk_ignored);
    RUN(test_incomplete_not_ready);
    RUN(test_split_capacity_overflow);
    RUN(test_reset_clears_state);
    RUN(test_null_safety);
    printf("%d/%d tests passed\n", g_pass_count,
           g_pass_count + g_fail_count);
    return g_fail_count ? 1 : 0;
}
