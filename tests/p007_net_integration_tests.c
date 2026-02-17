/**
 * @file p007_net_integration_tests.c
 * @brief Integration tests wiring validation → ghost → interest →
 *        snapshot_delta → chunking → time_sync → prediction.
 *
 * Each test exercises multiple net modules cooperating in a realistic
 * pipeline scenario rather than testing modules in isolation.
 */

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "ferrum/net/validation.h"
#include "ferrum/net/ghost_table.h"
#include "ferrum/net/snapshot_delta.h"
#include "ferrum/net/snapshot_chunk.h"
#include "ferrum/net/interest.h"
#include "ferrum/net/time_sync.h"
#include "ferrum/net/prediction.h"

/* ------------------------------------------------------------------ */
/*  Minimal test harness                                              */
/* ------------------------------------------------------------------ */

static int g_pass = 0, g_fail = 0;

#define TEST(name) static void name(void)
#define RUN(name)                                                      \
    do {                                                               \
        printf("  %-52s ", #name);                                     \
        name();                                                        \
        printf("PASS\n");                                              \
        g_pass++;                                                      \
    } while (0)

#define ASSERT(cond)                                                   \
    do {                                                               \
        if (!(cond)) {                                                 \
            printf("FAIL (%s:%d): %s\n", __FILE__, __LINE__, #cond);   \
            g_fail++;                                                  \
            return;                                                    \
        }                                                              \
    } while (0)

/* ------------------------------------------------------------------ */
/*  Helpers: packet construction for validation gate                  */
/* ------------------------------------------------------------------ */

#define PROTO_ID     0x52555038u   /* 'RUP8' */
#define SCHEMA_SNAP  0x2001
#define SCHEMA_INPUT 0x200B

static void write_u32_be(uint8_t *buf, uint32_t v) {
    buf[0] = (uint8_t)(v >> 24);
    buf[1] = (uint8_t)(v >> 16);
    buf[2] = (uint8_t)(v >> 8);
    buf[3] = (uint8_t)(v);
}

static void write_u16_be(uint8_t *buf, uint16_t v) {
    buf[0] = (uint8_t)(v >> 8);
    buf[1] = (uint8_t)(v);
}

/**
 * Builds a packet: [proto:4][seq:2][ack:2][ack_bits:32][flags:1][rsv:1][schema:2][payload_len:2][rsv:2][payload...]
 * Packet header = 40 bytes, wire frame header = 8 bytes, total = 48 + payload.
 */
static size_t build_packet(uint8_t *out, size_t cap,
                           uint16_t schema_id,
                           const uint8_t *payload, uint16_t payload_len) {
    size_t total = 48 + (size_t)payload_len;
    if (total > cap) { return 0; }
    memset(out, 0, 48);
    write_u32_be(out + 0, PROTO_ID);      /* protocol id */
    write_u16_be(out + 4, 1);             /* seq */
    write_u16_be(out + 6, 0);             /* ack */
    /* ack_bits[4] at bytes 8..39 — zeroed by memset */
    /* Wire frame header at offset 40 */
    out[40] = 0;                          /* flags */
    out[41] = 0;                          /* reserved */
    write_u16_be(out + 42, schema_id);    /* schema id */
    write_u16_be(out + 44, payload_len);  /* payload size */
    write_u16_be(out + 46, 0);            /* reserved */
    if (payload && payload_len > 0) {
        memcpy(out + 48, payload, payload_len);
    }
    return total;
}

/* ------------------------------------------------------------------ */
/*  1. Server→client replication with ghosts                         */
/*     Validates → ghost_table create → snapshot_delta compute.       */
/* ------------------------------------------------------------------ */

TEST(test_server_replication_pipeline) {
    /* --- Gate: validate incoming snapshot packet --- */
    net_validation_ctx_t val;
    uint16_t schemas[] = { SCHEMA_SNAP, SCHEMA_INPUT };
    net_validation_init(&val, PROTO_ID, schemas, 2);

    uint8_t pkt[64];
    uint8_t payload[] = { 0xAA, 0xBB }; /* dummy snapshot payload */
    size_t sz = build_packet(pkt, sizeof(pkt), SCHEMA_SNAP, payload, 2);
    ASSERT(sz > 0);
    ASSERT(net_validation_check(&val, pkt, sz) == NET_VALIDATION_OK);

    /* --- Register server entities in ghost table --- */
    net_ghost_entry_t entries[8];
    net_ghost_table_t ghosts;
    net_ghost_table_init(&ghosts, entries, 8);

    /* Server tells us about 3 bodies: ids 100, 200, 300 */
    net_ghost_entity_t local_a = { .index = 0, .generation = 1 };
    net_ghost_entity_t local_b = { .index = 1, .generation = 1 };
    net_ghost_entity_t local_c = { .index = 2, .generation = 1 };
    ASSERT(net_ghost_table_create(&ghosts, 100, local_a) == NET_GHOST_OK);
    ASSERT(net_ghost_table_create(&ghosts, 200, local_b) == NET_GHOST_OK);
    ASSERT(net_ghost_table_create(&ghosts, 300, local_c) == NET_GHOST_OK);
    ASSERT(net_ghost_table_count(&ghosts) == 3);

    /* --- Delta replication: baseline → current --- */
    net_snap_body_t base_bodies[3] = {
        { .body_id = 100, .position = {0, 0, 0} },
        { .body_id = 200, .position = {1000, 0, 0} },
        { .body_id = 300, .position = {2000, 0, 0} },
    };
    net_snapshot_t baseline = { .tick = 1, .bodies = base_bodies, .body_count = 3 };

    net_snap_body_t cur_bodies[3] = {
        { .body_id = 100, .position = {10, 0, 0} },     /* moved */
        { .body_id = 200, .position = {1000, 0, 0} },   /* unchanged */
        { .body_id = 300, .position = {2000, 5, 0} },   /* moved */
    };
    net_snapshot_t current = { .tick = 2, .bodies = cur_bodies, .body_count = 3 };

    net_snap_delta_entry_t delta_entries[8];
    net_snapshot_delta_t delta = { .entries = delta_entries, .capacity = 8 };
    int rc = net_snapshot_delta_compute(&baseline, &current, &delta);
    ASSERT(rc == 0);

    /* Body 100 and 300 changed, 200 didn't */
    int found_100 = 0, found_300 = 0;
    for (uint32_t i = 0; i < delta.count; i++) {
        if (delta_entries[i].body_id == 100) { found_100 = 1; }
        if (delta_entries[i].body_id == 300) { found_300 = 1; }
        /* 200 should NOT be in delta */
        ASSERT(delta_entries[i].body_id != 200);
    }
    ASSERT(found_100 && found_300);

    /* Lookup ghost mapping for changed bodies */
    net_ghost_entity_t out;
    ASSERT(net_ghost_table_lookup(&ghosts, 100, &out) == NET_GHOST_OK);
    ASSERT(out.index == 0);
    ASSERT(net_ghost_table_lookup(&ghosts, 300, &out) == NET_GHOST_OK);
    ASSERT(out.index == 2);

    /* Validation stats confirm 1 valid packet */
    ASSERT(val.stats.packets_valid == 1);
}

/* ------------------------------------------------------------------ */
/*  2. Predicted movement → server correction → reconciliation        */
/*     prediction + time_sync cooperating.                            */
/* ------------------------------------------------------------------ */

static void test_sim_step(net_predict_state_t *state,
                          const net_predict_input_t *input,
                          void *user) {
    (void)user;
    /* Simple: position += input.move per tick */
    state->position[0] += input->move[0];
    state->position[1] += input->move[1];
    state->position[2] += input->move[2];
}

TEST(test_prediction_reconciliation_flow) {
    /* --- Time sync: establish server offset --- */
    net_time_sync_t sync;
    net_time_sync_init(&sync, 8, 50);

    /* Simulate several sync packets */
    for (int i = 0; i < 8; i++) {
        int64_t server_t = 10000 + i * 16;
        int64_t client_t = i * 16;           /* offset ~10000 */
        net_time_sync_sample(&sync, server_t, client_t);
    }
    int64_t offset = net_time_sync_offset(&sync);
    /* Offset should be approximately 10000 */
    ASSERT(offset > 9800 && offset < 10200);

    /* --- Prediction: record local inputs --- */
    net_predict_input_t ring_buf[16];
    net_predict_config_t cfg = {
        .snap_threshold = 100.0f,    /* snap if error > 100 */
        .blend_threshold = 1.0f,     /* blend if error > 1 */
        .blend_rate = 0.5f,
    };
    net_predict_ctx_t pred;
    net_predict_init(&pred, ring_buf, 16, &cfg, test_sim_step, NULL);

    /* Push 5 ticks of rightward movement */
    for (uint32_t t = 1; t <= 5; t++) {
        net_predict_input_t inp = { .tick = t, .move = {1.0f, 0.0f, 0.0f} };
        net_predict_input_ring_push(&pred.input_ring, &inp);
        /* Locally simulate */
        test_sim_step(&pred.predicted, &inp, NULL);
        pred.predicted_tick = t + 1;  /* predict is now at tick after this one */
    }
    /* Local prediction: position[0] should be 5.0 */
    ASSERT(fabsf(pred.predicted.position[0] - 5.0f) < 0.01f);

    /* --- Server authoritative state arrives for tick 3 --- */
    /* Server says position[0]=3 at tick 3 (matches our prediction exactly) */
    net_predict_state_t server_state = { .position = {3.0f, 0.0f, 0.0f} };
    int result = net_predict_reconcile(&pred, &server_state, 3);
    /* No correction needed — prediction matched server */
    ASSERT(result == 0);  /* NET_PREDICT_NONE */

    /* After reconciliation, final state should still be ~5.0
     * (replayed ticks 4 and 5 on top of server tick 3) */
    ASSERT(fabsf(pred.predicted.position[0] - 5.0f) < 0.01f);
}

/* ------------------------------------------------------------------ */
/*  3. Interest filtering → delta → chunking pipeline                 */
/*     interest → snapshot_delta → snapshot_chunk cooperating.         */
/* ------------------------------------------------------------------ */

TEST(test_interest_delta_chunk_pipeline) {
    /* --- Interest: select nearby dirty entities --- */
    net_interest_entity_t entities[6] = {
        { .entity_id = 10, .pos = {0.0f, 0.0f, 0.0f},
          .serialized_size = 26, .dirty = 1 },
        { .entity_id = 20, .pos = {5.0f, 0.0f, 0.0f},
          .serialized_size = 26, .dirty = 1 },
        { .entity_id = 30, .pos = {100.0f, 0.0f, 0.0f},
          .serialized_size = 26, .dirty = 1 },    /* far away → filtered */
        { .entity_id = 40, .pos = {3.0f, 0.0f, 0.0f},
          .serialized_size = 26, .dirty = 0 },     /* not dirty → filtered */
        { .entity_id = 50, .pos = {2.0f, 0.0f, 0.0f},
          .serialized_size = 26, .dirty = 1 },
        { .entity_id = 60, .pos = {1.0f, 0.0f, 0.0f},
          .serialized_size = 26, .dirty = 1 },
    };
    float viewpoint[3] = { 0.0f, 0.0f, 0.0f };
    net_interest_config_t icfg = {
        .radius = 50.0f,
        .budget_bytes = 200,     /* enough for ~7 bodies */
    };
    uint16_t result_ids[16];
    net_interest_result_t result = {
        .entity_ids = result_ids,
        .capacity = 16,
    };
    int rc = net_interest_query(entities, 6, viewpoint, &icfg, &result);
    ASSERT(rc == 0);
    /* Should include ids 10, 20, 50, 60 (dirty + within radius) */
    /* Excludes 30 (too far) and 40 (not dirty) */
    ASSERT(result.count == 4);
    /* Sorted by distance: 10 (d=0), 60 (d=1), 50 (d=2), 20 (d=5) */
    ASSERT(result_ids[0] == 10);
    ASSERT(result_ids[1] == 60);
    ASSERT(result_ids[2] == 50);
    ASSERT(result_ids[3] == 20);

    /* --- Delta: compute changes for selected bodies --- */
    /* Build baseline and current snapshots for selected entities */
    net_snap_body_t base_bodies[4] = {
        { .body_id = 10, .position = {0, 0, 0}, .flags = 0 },
        { .body_id = 60, .position = {100, 0, 0}, .flags = 0 },
        { .body_id = 50, .position = {200, 0, 0}, .flags = 0 },
        { .body_id = 20, .position = {500, 0, 0}, .flags = 0 },
    };
    net_snap_body_t cur_bodies[4] = {
        { .body_id = 10, .position = {5, 0, 0}, .flags = 0 },      /* moved */
        { .body_id = 60, .position = {105, 0, 0}, .flags = 0 },    /* moved */
        { .body_id = 50, .position = {200, 0, 0}, .flags = 0 },    /* same */
        { .body_id = 20, .position = {510, 0, 0}, .flags = 0 },    /* moved */
    };
    net_snapshot_t baseline = { .tick = 10, .bodies = base_bodies, .body_count = 4 };
    net_snapshot_t current  = { .tick = 11, .bodies = cur_bodies, .body_count = 4 };

    net_snap_delta_entry_t delta_entries[8];
    net_snapshot_delta_t delta = { .entries = delta_entries, .capacity = 8 };
    rc = net_snapshot_delta_compute(&baseline, &current, &delta);
    ASSERT(rc == 0);
    /* 3 bodies changed (10, 60, 20), body 50 unchanged */
    ASSERT(delta.count == 3);

    /* --- Chunk: split a large-ish payload --- */
    /* Simulate serialized delta payload (just use raw bytes) */
    uint8_t payload[256];
    memset(payload, 0xCD, sizeof(payload));

    net_chunk_header_t headers[16];
    uint32_t chunk_count = 0;
    rc = net_snapshot_chunk_split(payload, 256, 64,
                                 headers, 16, &chunk_count);
    ASSERT(rc == 0);
    ASSERT(chunk_count == 4);  /* 256 / 64 = 4 chunks */

    /* Reassemble on receiving side (out of order) */
    uint8_t reassembly_buf[512];
    net_chunk_reassembly_t reasm;
    net_chunk_reassembly_init(&reasm, reassembly_buf, sizeof(reassembly_buf));

    /* Push chunk 2, then 0, then 3, then 1 */
    int order[] = { 2, 0, 3, 1 };
    for (int i = 0; i < 4; i++) {
        int idx = order[i];
        int push_rc = net_chunk_reassembly_push(
            &reasm, &headers[idx],
            payload + headers[idx].offset,
            headers[idx].length);
        if (i < 3) {
            ASSERT(push_rc == NET_CHUNK_NOT_READY);
        } else {
            ASSERT(push_rc == NET_CHUNK_READY);  /* all received */
        }
    }

    /* Verify reassembled data matches original */
    ASSERT(memcmp(reassembly_buf, payload, 256) == 0);
}

/* ------------------------------------------------------------------ */
/*  4. Baseline tracker + ACK + delta recompute                       */
/*     snapshot_baseline record → ack → delta from ACKed baseline.    */
/* ------------------------------------------------------------------ */

TEST(test_baseline_ack_delta_cycle) {
    /* Set up baseline tracker */
    net_snap_body_t bl_bodies[4];
    net_snap_body_t ring_bodies[4 * 8];  /* 8 slots × 4 bodies */
    net_snapshot_t ring_snaps[8];

    net_snap_baseline_t bl;
    net_snap_baseline_init(&bl, bl_bodies, 4,
                           ring_snaps, ring_bodies, 4, 8);

    /* Record snapshots for ticks 1..5 */
    for (uint32_t t = 1; t <= 5; t++) {
        net_snap_body_t bodies[2] = {
            { .body_id = 1, .position = {(int16_t)(t * 10), 0, 0} },
            { .body_id = 2, .position = {(int16_t)(t * 20), 0, 0} },
        };
        net_snapshot_t snap = { .tick = t, .bodies = bodies, .body_count = 2 };
        int rc = net_snap_baseline_record(&bl, &snap);
        ASSERT(rc == 0);
    }

    /* Client ACKs tick 3 */
    int rc = net_snap_baseline_ack(&bl, 3);
    ASSERT(rc == 0);

    /* Baseline should now be tick 3 */
    ASSERT(bl.baseline.tick == 3);
    ASSERT(bl.baseline.bodies[0].position[0] == 30);  /* 3 * 10 */
    ASSERT(bl.baseline.bodies[1].position[0] == 60);  /* 3 * 20 */

    /* Compute delta from ACKed baseline to tick 5 */
    /* Find tick 5 in ring */
    net_snap_body_t tick5_bodies[2] = {
        { .body_id = 1, .position = {50, 0, 0} },
        { .body_id = 2, .position = {100, 0, 0} },
    };
    net_snapshot_t tick5 = { .tick = 5, .bodies = tick5_bodies, .body_count = 2 };

    net_snap_delta_entry_t delta_entries[4];
    net_snapshot_delta_t delta = { .entries = delta_entries, .capacity = 4 };
    rc = net_snapshot_delta_compute(&bl.baseline, &tick5, &delta);
    ASSERT(rc == 0);
    /* Both bodies changed from tick 3 → tick 5 */
    ASSERT(delta.count == 2);
}

/* ------------------------------------------------------------------ */
/*  5. Validation rejects bad packets before pipeline runs            */
/*     validation gate protects downstream modules.                   */
/* ------------------------------------------------------------------ */

TEST(test_validation_gates_pipeline) {
    net_validation_ctx_t val;
    uint16_t schemas[] = { SCHEMA_SNAP };
    net_validation_init(&val, PROTO_ID, schemas, 1);

    /* Bad protocol ID → rejected before ghost table */
    uint8_t bad_pkt[64];
    memset(bad_pkt, 0, sizeof(bad_pkt));
    write_u32_be(bad_pkt, 0xDEADBEEF);   /* wrong proto */
    write_u16_be(bad_pkt + 42, SCHEMA_SNAP);  /* schema in wire frame header */
    write_u16_be(bad_pkt + 44, 0);            /* payload_size */
    ASSERT(net_validation_check(&val, bad_pkt, 48) == NET_VALIDATION_ERR_PROTOCOL);

    /* Unknown schema → rejected */
    uint8_t pkt2[64];
    size_t sz2 = build_packet(pkt2, sizeof(pkt2), 0x9999, NULL, 0);
    ASSERT(net_validation_check(&val, pkt2, sz2) == NET_VALIDATION_ERR_SCHEMA);

    /* Truncated → rejected */
    ASSERT(net_validation_check(&val, bad_pkt, 4) == NET_VALIDATION_ERR_TRUNCATED);

    /* Valid → passes through */
    uint8_t good_pkt[64];
    size_t sz = build_packet(good_pkt, sizeof(good_pkt), SCHEMA_SNAP, NULL, 0);
    ASSERT(net_validation_check(&val, good_pkt, sz) == NET_VALIDATION_OK);

    /* Stats reflect the pipeline gating */
    ASSERT(val.stats.packets_total == 4);
    ASSERT(val.stats.packets_valid == 1);
    ASSERT(val.stats.protocol_errors == 1);
    ASSERT(val.stats.unknown_schemas == 1);
    ASSERT(val.stats.truncated_packets == 1);
}

/* ------------------------------------------------------------------ */
/*  6. Time sync + jitter buffer cooperation                          */
/*     Sync provides offset, jitter buffer provides safety margin.    */
/* ------------------------------------------------------------------ */

TEST(test_time_sync_with_jitter) {
    net_time_sync_t sync;
    net_time_sync_init(&sync, 8, 100);

    net_jitter_buffer_t jbuf;
    net_jitter_buffer_init(&jbuf, 8);

    /* Simulate 8 packets arriving with minor jitter */
    for (int i = 0; i < 8; i++) {
        int64_t server_t = 5000 + i * 16;
        int64_t client_t = i * 16;
        net_time_sync_sample(&sync, server_t, client_t);

        /* Packet expected every 16ms, actual has small positive jitter */
        uint64_t expected = (uint64_t)(i * 16);
        uint64_t actual = (uint64_t)(i * 16 + (i % 3));  /* 0..2ms jitter */
        net_jitter_buffer_sample(&jbuf, expected, actual);
    }

    int64_t offset = net_time_sync_offset(&sync);
    uint64_t margin = net_jitter_buffer_margin(&jbuf);

    /* Offset should be ~5000 */
    ASSERT(offset > 4800 && offset < 5200);
    /* Jitter margin should be small (≤5ms for ±2 jitter) */
    ASSERT(margin <= 5);

    /* A client can use: server_time = client_time + offset + margin */
    int64_t estimated_server = 200 + offset + (int64_t)margin;
    /* Should be close to 5200 */
    ASSERT(estimated_server > 5100 && estimated_server < 5300);
}

/* ------------------------------------------------------------------ */
/*  7. Entity spawn → ghost → destroy cycle                          */
/*     ghost_table + snapshot_delta for entity lifecycle.             */
/* ------------------------------------------------------------------ */

TEST(test_entity_lifecycle_spawn_destroy) {
    /* --- Ghost table tracks spawns --- */
    net_ghost_entry_t entries[8];
    net_ghost_table_t ghosts;
    net_ghost_table_init(&ghosts, entries, 8);

    /* Spawn entity 42 */
    net_ghost_entity_t local = { .index = 7, .generation = 1 };
    ASSERT(net_ghost_table_create(&ghosts, 42, local) == NET_GHOST_OK);
    ASSERT(net_ghost_table_count(&ghosts) == 1);

    /* --- Delta: new entity appears (not in baseline) --- */
    net_snap_body_t base_bodies[1] = {
        { .body_id = 99, .position = {0, 0, 0} },
    };
    net_snap_body_t cur_bodies[2] = {
        { .body_id = 99, .position = {0, 0, 0} },
        { .body_id = 42, .position = {500, 100, 0} },  /* newly spawned */
    };
    net_snapshot_t baseline = { .tick = 1, .bodies = base_bodies, .body_count = 1 };
    net_snapshot_t current  = { .tick = 2, .bodies = cur_bodies, .body_count = 2 };

    net_snap_delta_entry_t delta_entries[8];
    net_snapshot_delta_t delta = { .entries = delta_entries, .capacity = 8 };
    int rc = net_snapshot_delta_compute(&baseline, &current, &delta);
    ASSERT(rc == 0);
    /* Body 42 is new → should appear with all fields changed */
    int found_42 = 0;
    for (uint32_t i = 0; i < delta.count; i++) {
        if (delta_entries[i].body_id == 42) {
            found_42 = 1;
            /* New body should have POS and ORI at minimum */
            ASSERT(delta_entries[i].changed_mask & NET_SNAP_CHANGED_POS);
        }
    }
    ASSERT(found_42);

    /* --- Destroy: entity removed from next snapshot --- */
    net_snap_body_t cur2_bodies[1] = {
        { .body_id = 99, .position = {0, 0, 0} },
        /* body 42 gone */
    };
    net_snapshot_t current2 = { .tick = 3, .bodies = cur2_bodies, .body_count = 1 };

    net_snap_delta_entry_t delta2_entries[8];
    net_snapshot_delta_t delta2 = { .entries = delta2_entries, .capacity = 8 };

    /* Use tick-2 snapshot (which had body 42) as baseline */
    rc = net_snapshot_delta_compute(&current, &current2, &delta2);
    ASSERT(rc == 0);
    /* Body 42 should show DESTROY flag */
    int found_destroy = 0;
    for (uint32_t i = 0; i < delta2.count; i++) {
        if (delta2_entries[i].body_id == 42) {
            found_destroy = 1;
            ASSERT(delta2_entries[i].changed_mask & NET_SNAP_CHANGED_DESTROY);
        }
    }
    ASSERT(found_destroy);

    /* Clean up ghost table */
    ASSERT(net_ghost_table_destroy(&ghosts, 42) == NET_GHOST_OK);
    ASSERT(net_ghost_table_count(&ghosts) == 0);
}

/* ------------------------------------------------------------------ */
/*  8. Budget-limited interest → delta → chunk end-to-end             */
/*     Budget caps the number of entities, delta + chunk handles rest. */
/* ------------------------------------------------------------------ */

TEST(test_budget_limited_replication) {
    /* Many entities, tight budget → only closest get replicated */
    net_interest_entity_t entities[8];
    for (int i = 0; i < 8; i++) {
        entities[i].entity_id = (uint16_t)(i + 1);
        entities[i].pos[0] = (float)(i * 10);
        entities[i].pos[1] = 0.0f;
        entities[i].pos[2] = 0.0f;
        entities[i].serialized_size = 26;
        entities[i].dirty = 1;
    }

    float vp[3] = { 0.0f, 0.0f, 0.0f };
    net_interest_config_t icfg = {
        .radius = 1000.0f,       /* all in range */
        .budget_bytes = 80,      /* only fits ~3 bodies (3 * 26 = 78) */
    };
    uint16_t rids[16];
    net_interest_result_t result = { .entity_ids = rids, .capacity = 16 };
    int rc = net_interest_query(entities, 8, vp, &icfg, &result);
    ASSERT(rc == 0);
    /* Budget should cap to 3 entities */
    ASSERT(result.count == 3);
    /* Closest first: entity 1 (d=0), entity 2 (d=10), entity 3 (d=20) */
    ASSERT(rids[0] == 1);
    ASSERT(rids[1] == 2);
    ASSERT(rids[2] == 3);

    /* Build delta for selected bodies only */
    net_snap_body_t base_b[3], cur_b[3];
    for (uint32_t i = 0; i < 3; i++) {
        memset(&base_b[i], 0, sizeof(base_b[i]));
        base_b[i].body_id = rids[i];
        base_b[i].position[0] = (int16_t)(rids[i] * 100);

        cur_b[i] = base_b[i];
        cur_b[i].position[0] += 5;  /* all moved slightly */
    }
    net_snapshot_t baseline = { .tick = 1, .bodies = base_b, .body_count = 3 };
    net_snapshot_t current  = { .tick = 2, .bodies = cur_b, .body_count = 3 };

    net_snap_delta_entry_t dentries[8];
    net_snapshot_delta_t delta = { .entries = dentries, .capacity = 8 };
    rc = net_snapshot_delta_compute(&baseline, &current, &delta);
    ASSERT(rc == 0);
    ASSERT(delta.count == 3);

    /* Small delta → single chunk */
    /* Simulate serialized payload = count * entry_size ~ small */
    uint8_t payload[128];
    memset(payload, 0xAB, 64);  /* pretend 64 bytes of delta data */

    net_chunk_header_t headers[4];
    uint32_t chunk_count = 0;
    rc = net_snapshot_chunk_split(payload, 64, 128,
                                 headers, 4, &chunk_count);
    ASSERT(rc == 0);
    ASSERT(chunk_count == 1);  /* fits in one chunk */
}

/* ------------------------------------------------------------------ */
/*  Main                                                              */
/* ------------------------------------------------------------------ */

int main(void) {
    printf("p007_net_integration_tests:\n");
    RUN(test_server_replication_pipeline);
    RUN(test_prediction_reconciliation_flow);
    RUN(test_interest_delta_chunk_pipeline);
    RUN(test_baseline_ack_delta_cycle);
    RUN(test_validation_gates_pipeline);
    RUN(test_time_sync_with_jitter);
    RUN(test_entity_lifecycle_spawn_destroy);
    RUN(test_budget_limited_replication);
    printf("%d/%d tests passed\n", g_pass, g_pass + g_fail);
    return g_fail > 0 ? 1 : 0;
}
