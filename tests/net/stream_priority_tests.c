/**
 * @file stream_priority_tests.c
 * @brief Tests for the STREAM_PRIORITY protocol (rpg-3ldk): wire round-trip,
 *        failure modes, and end-to-end that applying server hints reorders the
 *        client streamer's admission.
 */
#include <stdio.h>
#include <string.h>

#include "ferrum/net/replication/common.h"
#include "ferrum/net/replication/stream_priority.h"
#include "ferrum/server/server_stream_priority.h"
#include "ferrum/asset/asset_stream.h"

#define ASSERT_TRUE(e) do { if (!(e)) { fprintf(stderr, \
    "  ASSERT_TRUE failed: %s (%s:%d)\n", #e, __FILE__, __LINE__); return 1; } } while (0)
#define ASSERT_INT_EQ(a,b) do { long _a=(long)(a),_b=(long)(b); if (_a!=_b) { \
    fprintf(stderr,"  ASSERT_INT_EQ failed: %ld != %ld (%s:%d)\n",_a,_b,__FILE__,__LINE__); \
    return 1; } } while (0)

static int test_wire_roundtrip(void)
{
    net_repl_stream_priority_t m; memset(&m, 0, sizeof m);
    m.count = 3;
    m.entries[0].id = 0x1122334455667788ull; m.entries[0].priority = 100;
    m.entries[1].id = 42;                     m.entries[1].priority = -7;
    m.entries[2].id = 0xFFFFFFFFFFFFFFFFull;  m.entries[2].priority = 2147483647;

    uint8_t buf[NET_REPL_STREAM_PRIORITY_MAX_PAYLOAD];
    size_t written = 0;
    ASSERT_INT_EQ(net_repl_stream_priority_encode(&m, buf, sizeof buf, &written), NET_REPL_OK);
    ASSERT_INT_EQ(written, 2u + 3u * 12u);
    /* One byte short fails. */
    ASSERT_INT_EQ(net_repl_stream_priority_encode(&m, buf, written - 1, NULL), NET_REPL_ERR_SHORT);

    net_repl_stream_priority_t r; memset(&r, 0xAB, sizeof r);
    ASSERT_INT_EQ(net_repl_stream_priority_decode(&r, buf, written), NET_REPL_OK);
    ASSERT_INT_EQ(r.count, 3);
    ASSERT_TRUE(r.entries[0].id == 0x1122334455667788ull);
    ASSERT_INT_EQ(r.entries[0].priority, 100);
    ASSERT_INT_EQ(r.entries[1].priority, -7);
    ASSERT_TRUE(r.entries[2].id == 0xFFFFFFFFFFFFFFFFull);
    ASSERT_INT_EQ(r.entries[2].priority, 2147483647);
    /* Truncated payload fails. */
    ASSERT_INT_EQ(net_repl_stream_priority_decode(&r, buf, written - 1), NET_REPL_ERR_SHORT);
    return 0;
}

/* Test load cb: returns the asset's size stored in slot_user. */
static size_t cb_load(void *u, uint64_t id, fr_asset_class_t c, void *su)
{ (void)u;(void)id;(void)c; return *(size_t *)su; }
static void cb_evict(void *u, uint64_t id, fr_asset_class_t c, void *su, int d)
{ (void)u;(void)id;(void)c;(void)su;(void)d; }

static int test_apply_reorders_admission(void)
{
    fr_asset_stream_config_t cfg; memset(&cfg, 0, sizeof cfg);
    cfg.ram_budget = 1000; cfg.max_in_flight = 4; cfg.capacity = 8;
    cfg.cbs.load = cb_load; cfg.cbs.evict = cb_evict;
    fr_asset_stream_t s; ASSERT_TRUE(fr_asset_stream_init(&s, &cfg));

    static size_t sizes[3] = { 1000, 1000, 1000 };
    /* All equal priority 0; budget fits only one. */
    for (int i = 0; i < 3; ++i)
        fr_asset_stream_add(&s, (uint64_t)(i + 1), FR_ASSET_SDF_CHUNK, 1000, 0, 0, &sizes[i]);

    /* Server hint: asset 3 is nearest the player -> highest priority. */
    net_repl_stream_priority_t hint; memset(&hint, 0, sizeof hint);
    hint.count = 3;
    hint.entries[0].id = 1; hint.entries[0].priority = 10;
    hint.entries[1].id = 2; hint.entries[1].priority = 20;
    hint.entries[2].id = 3; hint.entries[2].priority = 99;
    /* Round-trip the hint through the wire, as the client would. */
    uint8_t buf[NET_REPL_STREAM_PRIORITY_MAX_PAYLOAD]; size_t w = 0;
    net_repl_stream_priority_encode(&hint, buf, sizeof buf, &w);
    net_repl_stream_priority_t got;
    ASSERT_INT_EQ(net_repl_stream_priority_decode(&got, buf, w), NET_REPL_OK);

    ASSERT_INT_EQ(net_repl_stream_priority_apply(&got, &s), 3);
    fr_asset_stream_tick(&s);
    /* The highest-priority hinted asset (3) is the one that got resident. */
    ASSERT_INT_EQ(fr_asset_stream_residency(&s, 3), FR_RESIDENCY_RAM);
    ASSERT_INT_EQ(fr_asset_stream_residency(&s, 1), FR_RESIDENCY_ABSENT);
    ASSERT_INT_EQ(fr_asset_stream_residency(&s, 2), FR_RESIDENCY_ABSENT);
    fr_asset_stream_destroy(&s);
    return 0;
}

static int test_server_build_by_distance(void)
{
    uint64_t ids[3] = { 10, 20, 30 };
    float bmin[9] = { 0,0,0,  10,0,0,  100,0,0 };
    float bmax[9] = { 1,1,1,  11,1,1,  101,1,1 };
    float player[3] = { 0, 0, 0 };   /* nearest chunk 10, farthest 30. */
    net_repl_stream_priority_t m;
    ASSERT_INT_EQ(server_stream_priority_build(ids, bmin, bmax, 3, player, 1.0f, &m), 3);
    ASSERT_INT_EQ(m.count, 3);
    ASSERT_INT_EQ(m.entries[0].id, 10);
    /* Nearer => higher priority: 10 > 20 > 30. */
    ASSERT_TRUE(m.entries[0].priority > m.entries[1].priority);
    ASSERT_TRUE(m.entries[1].priority > m.entries[2].priority);
    ASSERT_INT_EQ(m.entries[0].priority, 0);   /* player inside chunk 10 -> dist 0. */
    return 0;
}

int main(void)
{
    struct { const char *name; int (*fn)(void); } tests[] = {
        {"wire_roundtrip",         test_wire_roundtrip},
        {"apply_reorders_admission", test_apply_reorders_admission},
        {"server_build_by_distance", test_server_build_by_distance},
    };
    int n = (int)(sizeof tests / sizeof tests[0]), fails = 0;
    for (int i = 0; i < n; ++i) {
        int r = tests[i].fn();
        fprintf(stderr, "[%s] %s\n", r ? "FAIL" : "ok  ", tests[i].name);
        fails += (r != 0);
    }
    fprintf(stderr, "\nstream_priority_tests: %d/%d passed\n", n - fails, n);
    return fails ? 1 : 0;
}
