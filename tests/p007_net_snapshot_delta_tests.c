/**
 * @file p007_net_snapshot_delta_tests.c
 * @brief RED tests for snapshot baseline tracking and delta replication.
 *
 * Covers:
 *   1. Delta from baseline reaches target state
 *   2. Client ACKs snapshot IDs; baseline advances
 *   3. Partial component update merges correctly
 *   4. Missing baseline fallback trigger
 *   5. Edge cases: empty snapshots, capacity, null safety
 *
 * Uses net_snapshot_baseline_t to track per-client baseline state
 * and net_snapshot_delta_t to represent the diff between two snapshots.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/net/snapshot_delta.h"

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

/** Build a snapshot body with easily identifiable values. */
static net_snap_body_t make_body(uint16_t id, int16_t px, int16_t py,
                                 int16_t pz, uint8_t flags) {
    net_snap_body_t b;
    memset(&b, 0, sizeof(b));
    b.body_id = id;
    b.position[0] = px;
    b.position[1] = py;
    b.position[2] = pz;
    b.flags = flags;
    return b;
}

/* ── Tests ──────────────────────────────────────────────────────── */

/**
 * Apply a delta to a baseline snapshot and verify the result
 * matches the target (current) snapshot exactly.
 */
TEST(test_delta_reaches_target_state) {
    /* Baseline: body 0 at (100, 200, 300). */
    net_snap_body_t base_bodies[4];
    base_bodies[0] = make_body(0, 100, 200, 300, 0);

    net_snapshot_t baseline = {
        .tick = 10,
        .body_count = 1,
        .bodies = base_bodies
    };

    /* Current: body 0 moved to (150, 200, 350). */
    net_snap_body_t cur_bodies[4];
    cur_bodies[0] = make_body(0, 150, 200, 350, 0);

    net_snapshot_t current = {
        .tick = 15,
        .body_count = 1,
        .bodies = cur_bodies
    };

    /* Compute delta. */
    net_snap_delta_entry_t delta_buf[4];
    net_snapshot_delta_t delta = {
        .entries = delta_buf,
        .capacity = 4,
        .count = 0
    };

    int rc = net_snapshot_delta_compute(&baseline, &current, &delta);
    ASSERT_EQ(rc, NET_SNAP_OK);
    ASSERT_EQ(delta.count, 1);
    ASSERT_EQ(delta.base_tick, 10);
    ASSERT_EQ(delta.cur_tick, 15);

    /* The delta entry should carry the changed fields for body 0. */
    ASSERT_EQ(delta.entries[0].body_id, 0);
    ASSERT(delta.entries[0].changed_mask & NET_SNAP_CHANGED_POS);
    /* Orientation didn't change — mask bit should be clear. */
    ASSERT(!(delta.entries[0].changed_mask & NET_SNAP_CHANGED_ORI));

    /* Apply delta to a copy of baseline → should produce current state. */
    net_snap_body_t result_bodies[4];
    memcpy(result_bodies, base_bodies, sizeof(base_bodies));

    net_snapshot_t result = {
        .tick = baseline.tick,
        .body_count = 1,
        .bodies = result_bodies
    };

    rc = net_snapshot_delta_apply(&result, &delta);
    ASSERT_EQ(rc, NET_SNAP_OK);
    ASSERT_EQ(result.tick, 15);
    ASSERT_EQ(result.bodies[0].position[0], 150);
    ASSERT_EQ(result.bodies[0].position[1], 200);
    ASSERT_EQ(result.bodies[0].position[2], 350);
}

/**
 * Baseline tracker advances its stored baseline when the client
 * ACKs a snapshot tick.
 */
TEST(test_baseline_advances_on_ack) {
    net_snap_body_t storage[8];
    net_snapshot_t ring_buf[4];  /* ring of snapshot history */
    net_snap_body_t ring_body_storage[4 * 8];

    net_snap_baseline_t bl;
    net_snap_baseline_init(&bl, storage, 8, ring_buf, ring_body_storage,
                           8, 4);

    /* Record snapshot at tick 10. */
    net_snap_body_t s10[1];
    s10[0] = make_body(0, 100, 200, 300, 0);
    net_snapshot_t snap10 = { .tick = 10, .body_count = 1, .bodies = s10 };
    int rc = net_snap_baseline_record(&bl, &snap10);
    ASSERT_EQ(rc, NET_SNAP_OK);

    /* Record snapshot at tick 15. */
    net_snap_body_t s15[1];
    s15[0] = make_body(0, 150, 200, 350, 0);
    net_snapshot_t snap15 = { .tick = 15, .body_count = 1, .bodies = s15 };
    rc = net_snap_baseline_record(&bl, &snap15);
    ASSERT_EQ(rc, NET_SNAP_OK);

    /* Before any ACK, baseline tick should be 0 (no baseline). */
    ASSERT_EQ(bl.baseline_tick, 0);

    /* Client ACKs tick 10. */
    rc = net_snap_baseline_ack(&bl, 10);
    ASSERT_EQ(rc, NET_SNAP_OK);
    ASSERT_EQ(bl.baseline_tick, 10);

    /* Baseline body state should match tick 10's snapshot. */
    ASSERT_EQ(bl.baseline.bodies[0].position[0], 100);

    /* Client ACKs tick 15 — baseline advances. */
    rc = net_snap_baseline_ack(&bl, 15);
    ASSERT_EQ(rc, NET_SNAP_OK);
    ASSERT_EQ(bl.baseline_tick, 15);
    ASSERT_EQ(bl.baseline.bodies[0].position[0], 150);
}

/**
 * Only changed components appear in the delta; unchanged fields
 * in the target are not modified on apply.
 */
TEST(test_partial_update_merges) {
    net_snap_body_t base_bodies[4];
    base_bodies[0] = make_body(0, 100, 200, 300, 0x01);
    /* Set some orientation. */
    base_bodies[0].orientation[0] = 1000;
    base_bodies[0].orientation[1] = 2000;
    base_bodies[0].orientation[2] = 3000;
    /* Set velocity. */
    base_bodies[0].linear_vel[0] = 500;

    net_snapshot_t baseline = {
        .tick = 20, .body_count = 1, .bodies = base_bodies
    };

    /* Current: only position changed; orientation + vel unchanged. */
    net_snap_body_t cur_bodies[4];
    cur_bodies[0] = base_bodies[0]; /* copy everything */
    cur_bodies[0].position[0] = 999; /* change only X position */

    net_snapshot_t current = {
        .tick = 25, .body_count = 1, .bodies = cur_bodies
    };

    net_snap_delta_entry_t delta_buf[4];
    net_snapshot_delta_t delta = {
        .entries = delta_buf, .capacity = 4, .count = 0
    };

    int rc = net_snapshot_delta_compute(&baseline, &current, &delta);
    ASSERT_EQ(rc, NET_SNAP_OK);
    ASSERT_EQ(delta.count, 1);
    ASSERT(delta.entries[0].changed_mask & NET_SNAP_CHANGED_POS);
    ASSERT(!(delta.entries[0].changed_mask & NET_SNAP_CHANGED_ORI));
    ASSERT(!(delta.entries[0].changed_mask & NET_SNAP_CHANGED_LINVEL));

    /* Apply to baseline copy → orientation and vel must survive. */
    net_snap_body_t result_bodies[4];
    memcpy(result_bodies, base_bodies, sizeof(net_snap_body_t));

    net_snapshot_t result = {
        .tick = 20, .body_count = 1, .bodies = result_bodies
    };

    rc = net_snapshot_delta_apply(&result, &delta);
    ASSERT_EQ(rc, NET_SNAP_OK);
    ASSERT_EQ(result.bodies[0].position[0], 999);
    /* Unchanged fields preserved. */
    ASSERT_EQ(result.bodies[0].orientation[0], 1000);
    ASSERT_EQ(result.bodies[0].orientation[1], 2000);
    ASSERT_EQ(result.bodies[0].orientation[2], 3000);
    ASSERT_EQ(result.bodies[0].linear_vel[0], 500);
    ASSERT_EQ(result.bodies[0].flags, 0x01);
}

/**
 * When the client's baseline_tick doesn't match any snapshot in
 * the ring buffer (too old / expired), a fallback to full baseline
 * send should be triggered.
 */
TEST(test_missing_baseline_fallback) {
    net_snap_body_t storage[8];
    net_snapshot_t ring_buf[2]; /* very small ring — only 2 history slots */
    net_snap_body_t ring_body_storage[2 * 8];

    net_snap_baseline_t bl;
    net_snap_baseline_init(&bl, storage, 8, ring_buf, ring_body_storage,
                           8, 2);

    /* Record ticks 10, 20, 30 — ring can only hold 2. */
    net_snap_body_t s10[1];
    s10[0] = make_body(0, 100, 0, 0, 0);
    net_snapshot_t snap10 = { .tick = 10, .body_count = 1, .bodies = s10 };
    net_snap_baseline_record(&bl, &snap10);

    net_snap_body_t s20[1];
    s20[0] = make_body(0, 200, 0, 0, 0);
    net_snapshot_t snap20 = { .tick = 20, .body_count = 1, .bodies = s20 };
    net_snap_baseline_record(&bl, &snap20);

    net_snap_body_t s30[1];
    s30[0] = make_body(0, 300, 0, 0, 0);
    net_snapshot_t snap30 = { .tick = 30, .body_count = 1, .bodies = s30 };
    net_snap_baseline_record(&bl, &snap30);

    /* Try to ACK tick 10 — should be expired from the ring. */
    int rc = net_snap_baseline_ack(&bl, 10);
    ASSERT_EQ(rc, NET_SNAP_BASELINE_EXPIRED);

    /* ACK tick 20 should still be in ring. */
    rc = net_snap_baseline_ack(&bl, 20);
    ASSERT_EQ(rc, NET_SNAP_OK);
}

/**
 * Delta with no changes produces an empty delta (count == 0).
 */
TEST(test_delta_no_changes) {
    net_snap_body_t bodies[2];
    bodies[0] = make_body(0, 100, 200, 300, 0);

    net_snapshot_t a = { .tick = 10, .body_count = 1, .bodies = bodies };

    /* Same state at a later tick. */
    net_snap_body_t bodies2[2];
    bodies2[0] = make_body(0, 100, 200, 300, 0);
    net_snapshot_t b = { .tick = 15, .body_count = 1, .bodies = bodies2 };

    net_snap_delta_entry_t delta_buf[4];
    net_snapshot_delta_t delta = {
        .entries = delta_buf, .capacity = 4, .count = 0
    };

    int rc = net_snapshot_delta_compute(&a, &b, &delta);
    ASSERT_EQ(rc, NET_SNAP_OK);
    ASSERT_EQ(delta.count, 0);
    ASSERT_EQ(delta.base_tick, 10);
    ASSERT_EQ(delta.cur_tick, 15);
}

/**
 * Bodies added in the current snapshot (not in baseline) appear
 * as fully-changed entries in the delta.
 */
TEST(test_delta_new_body_spawn) {
    /* Baseline has 1 body. */
    net_snap_body_t base[4];
    base[0] = make_body(0, 100, 200, 300, 0);
    net_snapshot_t baseline = { .tick = 10, .body_count = 1, .bodies = base };

    /* Current has 2 bodies — body 1 is new. */
    net_snap_body_t cur[4];
    cur[0] = make_body(0, 100, 200, 300, 0);
    cur[1] = make_body(1, 400, 500, 600, 0x02);
    net_snapshot_t current = { .tick = 15, .body_count = 2, .bodies = cur };

    net_snap_delta_entry_t delta_buf[4];
    net_snapshot_delta_t delta = {
        .entries = delta_buf, .capacity = 4, .count = 0
    };

    int rc = net_snapshot_delta_compute(&baseline, &current, &delta);
    ASSERT_EQ(rc, NET_SNAP_OK);
    /* Body 0 unchanged → no entry.  Body 1 is new → full entry. */
    ASSERT_EQ(delta.count, 1);
    ASSERT_EQ(delta.entries[0].body_id, 1);
    /* New body must have all fields flagged as changed. */
    ASSERT(delta.entries[0].changed_mask & NET_SNAP_CHANGED_POS);
    ASSERT(delta.entries[0].changed_mask & NET_SNAP_CHANGED_ORI);
    ASSERT(delta.entries[0].changed_mask & NET_SNAP_CHANGED_LINVEL);
    ASSERT(delta.entries[0].changed_mask & NET_SNAP_CHANGED_ANGVEL);
    ASSERT(delta.entries[0].changed_mask & NET_SNAP_CHANGED_FLAGS);
}

/**
 * Bodies present in the baseline but absent from current snapshot
 * appear as destroy entries in the delta.
 */
TEST(test_delta_body_destroy) {
    /* Baseline has 2 bodies. */
    net_snap_body_t base[4];
    base[0] = make_body(0, 100, 200, 300, 0);
    base[1] = make_body(1, 400, 500, 600, 0);
    net_snapshot_t baseline = { .tick = 10, .body_count = 2, .bodies = base };

    /* Current has only body 0 — body 1 destroyed. */
    net_snap_body_t cur[4];
    cur[0] = make_body(0, 100, 200, 300, 0);
    net_snapshot_t current = { .tick = 15, .body_count = 1, .bodies = cur };

    net_snap_delta_entry_t delta_buf[4];
    net_snapshot_delta_t delta = {
        .entries = delta_buf, .capacity = 4, .count = 0
    };

    int rc = net_snapshot_delta_compute(&baseline, &current, &delta);
    ASSERT_EQ(rc, NET_SNAP_OK);
    ASSERT_EQ(delta.count, 1);
    ASSERT_EQ(delta.entries[0].body_id, 1);
    ASSERT(delta.entries[0].changed_mask & NET_SNAP_CHANGED_DESTROY);
}

/**
 * Delta capacity overflow returns NET_SNAP_FULL.
 */
TEST(test_delta_capacity_overflow) {
    /* Baseline empty, current has 3 bodies, delta buffer holds only 2. */
    net_snap_body_t base[1];
    net_snapshot_t baseline = { .tick = 10, .body_count = 0, .bodies = base };

    net_snap_body_t cur[4];
    cur[0] = make_body(0, 100, 0, 0, 0);
    cur[1] = make_body(1, 200, 0, 0, 0);
    cur[2] = make_body(2, 300, 0, 0, 0);
    net_snapshot_t current = { .tick = 15, .body_count = 3, .bodies = cur };

    net_snap_delta_entry_t delta_buf[2]; /* too small */
    net_snapshot_delta_t delta = {
        .entries = delta_buf, .capacity = 2, .count = 0
    };

    int rc = net_snapshot_delta_compute(&baseline, &current, &delta);
    ASSERT_EQ(rc, NET_SNAP_FULL);
}

/**
 * NULL arguments return NET_SNAP_ERR_INVALID.
 */
TEST(test_null_safety) {
    net_snap_delta_entry_t delta_buf[4];
    net_snapshot_delta_t delta = {
        .entries = delta_buf, .capacity = 4, .count = 0
    };

    net_snap_body_t b[1];
    net_snapshot_t snap = { .tick = 1, .body_count = 0, .bodies = b };

    ASSERT_EQ(net_snapshot_delta_compute(NULL, &snap, &delta),
              NET_SNAP_ERR_INVALID);
    ASSERT_EQ(net_snapshot_delta_compute(&snap, NULL, &delta),
              NET_SNAP_ERR_INVALID);
    ASSERT_EQ(net_snapshot_delta_compute(&snap, &snap, NULL),
              NET_SNAP_ERR_INVALID);

    ASSERT_EQ(net_snapshot_delta_apply(NULL, &delta),
              NET_SNAP_ERR_INVALID);
    ASSERT_EQ(net_snapshot_delta_apply(&snap, NULL),
              NET_SNAP_ERR_INVALID);

    /* Baseline tracker NULL safety. */
    net_snap_baseline_init(NULL, NULL, 0, NULL, NULL, 0, 0);
    ASSERT_EQ(net_snap_baseline_record(NULL, &snap), NET_SNAP_ERR_INVALID);
    ASSERT_EQ(net_snap_baseline_ack(NULL, 1), NET_SNAP_ERR_INVALID);
}

/* ── Runner ─────────────────────────────────────────────────────── */

int main(void) {
    printf("p007_net_snapshot_delta_tests:\n");
    RUN(test_delta_reaches_target_state);
    RUN(test_baseline_advances_on_ack);
    RUN(test_partial_update_merges);
    RUN(test_missing_baseline_fallback);
    RUN(test_delta_no_changes);
    RUN(test_delta_new_body_spawn);
    RUN(test_delta_body_destroy);
    RUN(test_delta_capacity_overflow);
    RUN(test_null_safety);
    printf("%d/%d tests passed\n", g_pass_count,
           g_pass_count + g_fail_count);
    return g_fail_count ? 1 : 0;
}
