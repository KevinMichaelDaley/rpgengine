/**
 * @file p010_mesh_data_tests.c
 * @brief Tests for NET_REPL_MESH_DATA chunked mesh transfer.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ferrum/net/replication/mesh_data.h"

/* ── Test harness ─────────────────────────────────────────────── */

static int g_pass, g_fail;
#define RUN(fn) do { \
    printf("RUN  %s\n", #fn); \
    if (fn()) { printf("OK   %s\n", #fn); g_pass++; } \
    else      { printf("FAIL %s\n", #fn); g_fail++; } \
} while(0)
#define ASSERT(c) do { if (!(c)) { \
    printf("  ASSERT FAILED: %s (line %d)\n", #c, __LINE__); return 0; } \
} while(0)

/* ── Chunk encode/decode ──────────────────────────────────────── */

static int test_encode_decode_roundtrip(void) {
    uint8_t payload[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x42};
    net_repl_mesh_chunk_t src = {
        .body_id = 7,
        .chunk_index = 2,
        .total_chunks = 5,
        .total_size = 4800,
        .payload = payload,
        .payload_size = sizeof(payload),
    };

    uint8_t wire[1024];
    uint32_t n = net_repl_mesh_chunk_encode(&src, wire, sizeof(wire));
    ASSERT(n == NET_REPL_MESH_CHUNK_HEADER_SIZE + sizeof(payload));

    net_repl_mesh_chunk_t dst;
    memset(&dst, 0, sizeof(dst));
    int rc = net_repl_mesh_chunk_decode(&dst, wire, n);
    ASSERT(rc == NET_REPL_OK);
    ASSERT(dst.body_id == 7);
    ASSERT(dst.chunk_index == 2);
    ASSERT(dst.total_chunks == 5);
    ASSERT(dst.total_size == 4800);
    ASSERT(dst.payload_size == sizeof(payload));
    ASSERT(memcmp(dst.payload, payload, sizeof(payload)) == 0);
    return 1;
}

static int test_encode_null_params(void) {
    uint8_t wire[64];
    ASSERT(net_repl_mesh_chunk_encode(NULL, wire, sizeof(wire)) == 0);

    net_repl_mesh_chunk_t c = {.payload = (uint8_t *)"x", .payload_size = 1};
    ASSERT(net_repl_mesh_chunk_encode(&c, NULL, 64) == 0);
    return 1;
}

static int test_encode_buffer_too_small(void) {
    uint8_t payload[100];
    net_repl_mesh_chunk_t c = {
        .payload = payload, .payload_size = 100,
    };
    uint8_t wire[50]; /* too small for header + 100 */
    ASSERT(net_repl_mesh_chunk_encode(&c, wire, sizeof(wire)) == 0);
    return 1;
}

static int test_decode_truncated(void) {
    net_repl_mesh_chunk_t c;
    uint8_t wire[5] = {0}; /* less than header */
    ASSERT(net_repl_mesh_chunk_decode(&c, wire, sizeof(wire)) != NET_REPL_OK);
    return 1;
}

/* ── Send helper ──────────────────────────────────────────────── */

/* Collect chunks for verification. */
#define MAX_TEST_CHUNKS 64
static uint8_t g_chunk_bufs[MAX_TEST_CHUNKS][1024];
static size_t  g_chunk_lens[MAX_TEST_CHUNKS];
static uint32_t g_chunk_count;

static bool collect_chunk_(const uint8_t *wire, size_t wire_len, void *user) {
    (void)user;
    ASSERT(g_chunk_count < MAX_TEST_CHUNKS);
    memcpy(g_chunk_bufs[g_chunk_count], wire, wire_len);
    g_chunk_lens[g_chunk_count] = wire_len;
    g_chunk_count++;
    return true;
}

static int test_send_small_blob(void) {
    /* Small blob that fits in one chunk. */
    uint8_t blob[200];
    for (int i = 0; i < 200; i++) blob[i] = (uint8_t)(i & 0xFF);

    g_chunk_count = 0;
    uint32_t sent = net_repl_mesh_data_send(42, blob, 200, collect_chunk_, NULL);
    ASSERT(sent == 1);
    ASSERT(g_chunk_count == 1);

    /* Decode and verify — skip 2-byte schema prefix from send helper. */
    net_repl_mesh_chunk_t c;
    int rc = net_repl_mesh_chunk_decode(&c, g_chunk_bufs[0] + 2, g_chunk_lens[0] - 2);
    ASSERT(rc == NET_REPL_OK);
    ASSERT(c.body_id == 42);
    ASSERT(c.chunk_index == 0);
    ASSERT(c.total_chunks == 1);
    ASSERT(c.total_size == 200);
    ASSERT(c.payload_size == 200);
    ASSERT(memcmp(c.payload, blob, 200) == 0);
    return 1;
}

static int test_send_multichunk_blob(void) {
    /* Blob that needs multiple chunks. */
    uint32_t size = NET_REPL_MESH_CHUNK_MAX * 3 + 100;
    uint8_t *blob = malloc(size);
    ASSERT(blob);
    for (uint32_t i = 0; i < size; i++) blob[i] = (uint8_t)(i * 7);

    g_chunk_count = 0;
    uint32_t sent = net_repl_mesh_data_send(99, blob, size, collect_chunk_, NULL);
    ASSERT(sent == 4); /* 3 full + 1 partial */
    ASSERT(g_chunk_count == 4);

    /* Verify continuity. */
    uint8_t *reassembled = malloc(size);
    uint32_t offset = 0;
    for (uint32_t i = 0; i < 4; i++) {
        net_repl_mesh_chunk_t c;
        int rc = net_repl_mesh_chunk_decode(&c, g_chunk_bufs[i] + 2, g_chunk_lens[i] - 2);
        ASSERT(rc == NET_REPL_OK);
        ASSERT(c.body_id == 99);
        ASSERT(c.chunk_index == i);
        ASSERT(c.total_chunks == 4);
        ASSERT(c.total_size == size);
        memcpy(reassembled + offset, c.payload, c.payload_size);
        offset += c.payload_size;
    }
    ASSERT(offset == size);
    ASSERT(memcmp(reassembled, blob, size) == 0);

    free(blob);
    free(reassembled);
    return 1;
}

static int test_send_null_params(void) {
    ASSERT(net_repl_mesh_data_send(0, NULL, 100, collect_chunk_, NULL) == 0);
    uint8_t blob[10];
    ASSERT(net_repl_mesh_data_send(0, blob, 10, NULL, NULL) == 0);
    return 1;
}

static int test_send_zero_size(void) {
    ASSERT(net_repl_mesh_data_send(0, (uint8_t *)"x", 0, collect_chunk_, NULL) == 0);
    return 1;
}

static int test_send_abort_callback(void) {
    /* Callback returns false on second chunk → should abort. */
    uint32_t call_count = 0;
    bool abort_on_second(const uint8_t *w, size_t l, void *u) {
        (void)w; (void)l; (void)u;
        call_count++;
        return call_count < 2;
    }

    uint32_t size = NET_REPL_MESH_CHUNK_MAX * 3;
    uint8_t *blob = calloc(size, 1);
    uint32_t sent = net_repl_mesh_data_send(1, blob, size, abort_on_second, NULL);
    ASSERT(sent == 1); /* only first chunk sent successfully */
    free(blob);
    return 1;
}

/* ── Reassembly ───────────────────────────────────────────────── */

static int test_reassembly_single_chunk(void) {
    net_repl_mesh_reassembly_table_t table;
    ASSERT(net_repl_mesh_reassembly_init(&table, 4));

    uint8_t payload[] = {1, 2, 3, 4, 5};
    net_repl_mesh_chunk_t c = {
        .body_id = 10, .chunk_index = 0, .total_chunks = 1,
        .total_size = 5, .payload = payload, .payload_size = 5,
    };

    uint8_t *out = NULL;
    uint32_t out_size = 0;
    ASSERT(net_repl_mesh_reassembly_push(&table, &c, &out, &out_size));
    ASSERT(out != NULL);
    ASSERT(out_size == 5);
    ASSERT(memcmp(out, payload, 5) == 0);

    free(out);
    net_repl_mesh_reassembly_destroy(&table);
    return 1;
}

static int test_reassembly_multichunk(void) {
    net_repl_mesh_reassembly_table_t table;
    ASSERT(net_repl_mesh_reassembly_init(&table, 4));

    /* Use a blob large enough for multiple chunks. */
    uint32_t blob_size = NET_REPL_MESH_CHUNK_MAX * 2 + 50;
    uint8_t *blob = malloc(blob_size);
    for (uint32_t i = 0; i < blob_size; i++) blob[i] = (uint8_t)(i + 10);

    /* Chunk it manually. */
    uint32_t total_chunks = 3; /* 960 + 960 + 50 */
    uint8_t *out = NULL;
    uint32_t out_size = 0;

    /* Send chunks out of order: 1, 0, 2 */
    net_repl_mesh_chunk_t c;

    c = (net_repl_mesh_chunk_t){
        .body_id = 5, .chunk_index = 1, .total_chunks = (uint16_t)total_chunks,
        .total_size = blob_size,
        .payload = blob + NET_REPL_MESH_CHUNK_MAX,
        .payload_size = (uint16_t)NET_REPL_MESH_CHUNK_MAX,
    };
    ASSERT(!net_repl_mesh_reassembly_push(&table, &c, &out, &out_size));

    c = (net_repl_mesh_chunk_t){
        .body_id = 5, .chunk_index = 0, .total_chunks = (uint16_t)total_chunks,
        .total_size = blob_size,
        .payload = blob,
        .payload_size = (uint16_t)NET_REPL_MESH_CHUNK_MAX,
    };
    ASSERT(!net_repl_mesh_reassembly_push(&table, &c, &out, &out_size));

    c = (net_repl_mesh_chunk_t){
        .body_id = 5, .chunk_index = 2, .total_chunks = (uint16_t)total_chunks,
        .total_size = blob_size,
        .payload = blob + NET_REPL_MESH_CHUNK_MAX * 2,
        .payload_size = 50,
    };
    ASSERT(net_repl_mesh_reassembly_push(&table, &c, &out, &out_size));
    ASSERT(out_size == blob_size);
    ASSERT(memcmp(out, blob, blob_size) == 0);

    free(out);
    free(blob);
    net_repl_mesh_reassembly_destroy(&table);
    return 1;
}

static int test_reassembly_duplicate_chunk(void) {
    net_repl_mesh_reassembly_table_t table;
    ASSERT(net_repl_mesh_reassembly_init(&table, 4));

    /* Two-chunk mesh, total_size = 2 * chunk_max. */
    uint32_t total = NET_REPL_MESH_CHUNK_MAX * 2;
    uint8_t *blob = calloc(total, 1);
    blob[0] = 0xAA;
    blob[NET_REPL_MESH_CHUNK_MAX] = 0xCC;

    net_repl_mesh_chunk_t c = {
        .body_id = 3, .chunk_index = 0, .total_chunks = 2,
        .total_size = total,
        .payload = blob,
        .payload_size = (uint16_t)NET_REPL_MESH_CHUNK_MAX,
    };
    uint8_t *out = NULL;
    uint32_t out_size = 0;

    ASSERT(!net_repl_mesh_reassembly_push(&table, &c, &out, &out_size));
    /* Push same chunk again — should not complete. */
    ASSERT(!net_repl_mesh_reassembly_push(&table, &c, &out, &out_size));

    /* Now send chunk 1 to complete. */
    c.chunk_index = 1;
    c.payload = blob + NET_REPL_MESH_CHUNK_MAX;
    ASSERT(net_repl_mesh_reassembly_push(&table, &c, &out, &out_size));
    ASSERT(out_size == total);
    ASSERT(out[0] == 0xAA);
    ASSERT(out[NET_REPL_MESH_CHUNK_MAX] == 0xCC);

    free(out);
    free(blob);
    net_repl_mesh_reassembly_destroy(&table);
    return 1;
}

static int test_reassembly_full_roundtrip(void) {
    /* Use send helper to chunk, then reassemble. */
    uint32_t blob_size = 2500;
    uint8_t *blob = malloc(blob_size);
    for (uint32_t i = 0; i < blob_size; i++) blob[i] = (uint8_t)(i * 13);

    g_chunk_count = 0;
    uint32_t sent = net_repl_mesh_data_send(7, blob, blob_size, collect_chunk_, NULL);
    ASSERT(sent > 0);

    net_repl_mesh_reassembly_table_t table;
    ASSERT(net_repl_mesh_reassembly_init(&table, 4));

    uint8_t *out = NULL;
    uint32_t out_size = 0;
    for (uint32_t i = 0; i < g_chunk_count; i++) {
        net_repl_mesh_chunk_t c;
        /* Skip 2-byte schema prefix from send helper. */
        int rc = net_repl_mesh_chunk_decode(&c, g_chunk_bufs[i] + 2, g_chunk_lens[i] - 2);
        ASSERT(rc == NET_REPL_OK);
        bool done = net_repl_mesh_reassembly_push(&table, &c, &out, &out_size);
        if (i == g_chunk_count - 1) {
            ASSERT(done);
        } else {
            ASSERT(!done);
        }
    }

    ASSERT(out_size == blob_size);
    ASSERT(memcmp(out, blob, blob_size) == 0);

    free(out);
    free(blob);
    net_repl_mesh_reassembly_destroy(&table);
    return 1;
}

static int test_reassembly_null_params(void) {
    net_repl_mesh_reassembly_table_t table;
    ASSERT(net_repl_mesh_reassembly_init(&table, 4));

    uint8_t *out = NULL;
    uint32_t out_size = 0;
    ASSERT(!net_repl_mesh_reassembly_push(&table, NULL, &out, &out_size));
    ASSERT(!net_repl_mesh_reassembly_push(NULL, NULL, &out, &out_size));

    ASSERT(!net_repl_mesh_reassembly_init(NULL, 4));

    net_repl_mesh_reassembly_destroy(&table);
    return 1;
}

static int test_reassembly_slot_reuse(void) {
    /* Fill all slots, then complete one — slot should be reusable. */
    net_repl_mesh_reassembly_table_t table;
    ASSERT(net_repl_mesh_reassembly_init(&table, 2));

    uint8_t p1[] = {1};
    uint8_t p2[] = {2};
    net_repl_mesh_chunk_t c;
    uint8_t *out = NULL;
    uint32_t out_size = 0;

    /* Fill slot 0 with body 10 (incomplete). */
    c = (net_repl_mesh_chunk_t){
        .body_id = 10, .chunk_index = 0, .total_chunks = 2,
        .total_size = 2, .payload = p1, .payload_size = 1,
    };
    ASSERT(!net_repl_mesh_reassembly_push(&table, &c, &out, &out_size));

    /* Fill slot 1 with body 20 (complete in 1 chunk). */
    c = (net_repl_mesh_chunk_t){
        .body_id = 20, .chunk_index = 0, .total_chunks = 1,
        .total_size = 1, .payload = p2, .payload_size = 1,
    };
    ASSERT(net_repl_mesh_reassembly_push(&table, &c, &out, &out_size));
    ASSERT(out[0] == 2);
    free(out); out = NULL;

    /* Slot 1 should now be free for body 30. */
    c = (net_repl_mesh_chunk_t){
        .body_id = 30, .chunk_index = 0, .total_chunks = 1,
        .total_size = 1, .payload = p1, .payload_size = 1,
    };
    ASSERT(net_repl_mesh_reassembly_push(&table, &c, &out, &out_size));
    ASSERT(out[0] == 1);
    free(out);

    net_repl_mesh_reassembly_destroy(&table);
    return 1;
}

/* ── Main ─────────────────────────────────────────────────────── */

int main(void) {
    /* Encode / decode */
    RUN(test_encode_decode_roundtrip);
    RUN(test_encode_null_params);
    RUN(test_encode_buffer_too_small);
    RUN(test_decode_truncated);

    /* Send helper */
    RUN(test_send_small_blob);
    RUN(test_send_multichunk_blob);
    RUN(test_send_null_params);
    RUN(test_send_zero_size);
    RUN(test_send_abort_callback);

    /* Reassembly */
    RUN(test_reassembly_single_chunk);
    RUN(test_reassembly_multichunk);
    RUN(test_reassembly_duplicate_chunk);
    RUN(test_reassembly_full_roundtrip);
    RUN(test_reassembly_null_params);
    RUN(test_reassembly_slot_reuse);

    printf("\n%d / %d tests passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}
