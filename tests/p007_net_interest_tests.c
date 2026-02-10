/**
 * @file p007_net_interest_tests.c
 * @brief RED tests for interest management and bandwidth budgeting.
 *
 * Covers:
 *   1. Entities within radius are in the interest set
 *   2. Entities outside radius are excluded
 *   3. Interest set updates as viewpoint moves
 *   4. Budget caps bytes/tick — high-priority entities selected first
 *   5. Priority: closer entities rank higher
 *   6. Dirty-only filtering: unchanged entities skipped
 *   7. Deterministic selection for same-priority entities
 *   8. Empty world / zero budget edge cases
 *   9. NULL safety
 */

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "ferrum/net/interest.h"

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

static net_interest_entity_t make_ent(uint16_t id, float x, float y,
                                      float z, uint32_t size,
                                      uint8_t dirty) {
    net_interest_entity_t e;
    memset(&e, 0, sizeof(e));
    e.entity_id = id;
    e.pos[0] = x;
    e.pos[1] = y;
    e.pos[2] = z;
    e.serialized_size = size;
    e.dirty = dirty;
    return e;
}

/** Check if entity_id is in the result set. */
static int result_contains(const net_interest_result_t *r, uint16_t id) {
    for (uint32_t i = 0; i < r->count; i++) {
        if (r->entity_ids[i] == id) { return 1; }
    }
    return 0;
}

/* ── Tests ──────────────────────────────────────────────────────── */

/**
 * Entities within the interest radius are included.
 */
TEST(test_entities_within_radius) {
    net_interest_entity_t entities[4];
    entities[0] = make_ent(0, 10.0f, 0.0f, 0.0f, 26, 1);  /* dist=10 */
    entities[1] = make_ent(1, 50.0f, 0.0f, 0.0f, 26, 1);  /* dist=50 */
    entities[2] = make_ent(2, 90.0f, 0.0f, 0.0f, 26, 1);  /* dist=90 */

    float viewpoint[3] = {0.0f, 0.0f, 0.0f};

    net_interest_config_t cfg = {
        .radius = 100.0f,
        .budget_bytes = 10000,
    };

    uint16_t ids_out[4];
    net_interest_result_t result = {
        .entity_ids = ids_out,
        .capacity = 4,
        .count = 0
    };

    int rc = net_interest_query(entities, 3, viewpoint, &cfg, &result);
    ASSERT_EQ(rc, NET_INTEREST_OK);
    ASSERT_EQ(result.count, 3);
    ASSERT(result_contains(&result, 0));
    ASSERT(result_contains(&result, 1));
    ASSERT(result_contains(&result, 2));
}

/**
 * Entities outside the radius are excluded.
 */
TEST(test_entities_outside_radius) {
    net_interest_entity_t entities[3];
    entities[0] = make_ent(0, 10.0f, 0.0f, 0.0f, 26, 1);   /* in */
    entities[1] = make_ent(1, 200.0f, 0.0f, 0.0f, 26, 1);  /* out */
    entities[2] = make_ent(2, 0.0f, 150.0f, 0.0f, 26, 1);  /* out */

    float viewpoint[3] = {0.0f, 0.0f, 0.0f};

    net_interest_config_t cfg = {
        .radius = 100.0f,
        .budget_bytes = 10000,
    };

    uint16_t ids_out[4];
    net_interest_result_t result = {
        .entity_ids = ids_out, .capacity = 4, .count = 0
    };

    int rc = net_interest_query(entities, 3, viewpoint, &cfg, &result);
    ASSERT_EQ(rc, NET_INTEREST_OK);
    ASSERT_EQ(result.count, 1);
    ASSERT(result_contains(&result, 0));
}

/**
 * Moving the viewpoint changes which entities are in the set.
 */
TEST(test_viewpoint_movement) {
    net_interest_entity_t entities[3];
    entities[0] = make_ent(0, 0.0f, 0.0f, 0.0f, 26, 1);
    entities[1] = make_ent(1, 500.0f, 0.0f, 0.0f, 26, 1);

    net_interest_config_t cfg = {
        .radius = 100.0f,
        .budget_bytes = 10000,
    };

    uint16_t ids_out[4];
    net_interest_result_t result;

    /* Viewpoint at origin — only entity 0 in range. */
    float vp1[3] = {0.0f, 0.0f, 0.0f};
    result = (net_interest_result_t){
        .entity_ids = ids_out, .capacity = 4, .count = 0};
    net_interest_query(entities, 2, vp1, &cfg, &result);
    ASSERT_EQ(result.count, 1);
    ASSERT(result_contains(&result, 0));

    /* Viewpoint at (500,0,0) — only entity 1 in range. */
    float vp2[3] = {500.0f, 0.0f, 0.0f};
    result = (net_interest_result_t){
        .entity_ids = ids_out, .capacity = 4, .count = 0};
    net_interest_query(entities, 2, vp2, &cfg, &result);
    ASSERT_EQ(result.count, 1);
    ASSERT(result_contains(&result, 1));
}

/**
 * Budget limits the number of entities sent per tick.
 * Closer entities have higher priority and are selected first.
 */
TEST(test_budget_caps_output) {
    net_interest_entity_t entities[4];
    entities[0] = make_ent(0, 10.0f, 0.0f, 0.0f, 30, 1);  /* closest */
    entities[1] = make_ent(1, 50.0f, 0.0f, 0.0f, 30, 1);
    entities[2] = make_ent(2, 80.0f, 0.0f, 0.0f, 30, 1);  /* farthest in range */

    float viewpoint[3] = {0.0f, 0.0f, 0.0f};

    /* Budget allows only 2 entities (60 bytes). */
    net_interest_config_t cfg = {
        .radius = 100.0f,
        .budget_bytes = 60,
    };

    uint16_t ids_out[4];
    net_interest_result_t result = {
        .entity_ids = ids_out, .capacity = 4, .count = 0
    };

    int rc = net_interest_query(entities, 3, viewpoint, &cfg, &result);
    ASSERT_EQ(rc, NET_INTEREST_OK);
    ASSERT_EQ(result.count, 2);
    /* Closest two should be selected. */
    ASSERT(result_contains(&result, 0));
    ASSERT(result_contains(&result, 1));
    /* Farthest excluded by budget. */
    ASSERT(!result_contains(&result, 2));
}

/**
 * Priority ordering: closer entities appear first in results.
 */
TEST(test_priority_by_distance) {
    net_interest_entity_t entities[3];
    entities[0] = make_ent(0, 80.0f, 0.0f, 0.0f, 26, 1);  /* farthest */
    entities[1] = make_ent(1, 10.0f, 0.0f, 0.0f, 26, 1);  /* closest */
    entities[2] = make_ent(2, 40.0f, 0.0f, 0.0f, 26, 1);  /* middle */

    float viewpoint[3] = {0.0f, 0.0f, 0.0f};

    net_interest_config_t cfg = {
        .radius = 100.0f,
        .budget_bytes = 10000,
    };

    uint16_t ids_out[4];
    net_interest_result_t result = {
        .entity_ids = ids_out, .capacity = 4, .count = 0
    };

    net_interest_query(entities, 3, viewpoint, &cfg, &result);
    ASSERT_EQ(result.count, 3);
    /* Results should be sorted by distance: 1 (10), 2 (40), 0 (80). */
    ASSERT_EQ(result.entity_ids[0], 1);
    ASSERT_EQ(result.entity_ids[1], 2);
    ASSERT_EQ(result.entity_ids[2], 0);
}

/**
 * Non-dirty entities are excluded even if in range and budget allows.
 */
TEST(test_dirty_only_filtering) {
    net_interest_entity_t entities[3];
    entities[0] = make_ent(0, 10.0f, 0.0f, 0.0f, 26, 1); /* dirty */
    entities[1] = make_ent(1, 20.0f, 0.0f, 0.0f, 26, 0); /* clean */
    entities[2] = make_ent(2, 30.0f, 0.0f, 0.0f, 26, 1); /* dirty */

    float viewpoint[3] = {0.0f, 0.0f, 0.0f};

    net_interest_config_t cfg = {
        .radius = 100.0f,
        .budget_bytes = 10000,
    };

    uint16_t ids_out[4];
    net_interest_result_t result = {
        .entity_ids = ids_out, .capacity = 4, .count = 0
    };

    net_interest_query(entities, 3, viewpoint, &cfg, &result);
    ASSERT_EQ(result.count, 2);
    ASSERT(result_contains(&result, 0));
    ASSERT(!result_contains(&result, 1));
    ASSERT(result_contains(&result, 2));
}

/**
 * Empty entity list and zero budget produce empty results.
 */
TEST(test_edge_cases) {
    float viewpoint[3] = {0.0f, 0.0f, 0.0f};
    net_interest_config_t cfg = {
        .radius = 100.0f,
        .budget_bytes = 10000,
    };

    uint16_t ids_out[4];
    net_interest_result_t result = {
        .entity_ids = ids_out, .capacity = 4, .count = 0
    };

    /* Empty entity list. */
    int rc = net_interest_query(NULL, 0, viewpoint, &cfg, &result);
    ASSERT_EQ(rc, NET_INTEREST_OK);
    ASSERT_EQ(result.count, 0);

    /* Zero budget — nothing fits. */
    net_interest_entity_t entities[1];
    entities[0] = make_ent(0, 10.0f, 0.0f, 0.0f, 26, 1);

    cfg.budget_bytes = 0;
    result.count = 0;

    rc = net_interest_query(entities, 1, viewpoint, &cfg, &result);
    ASSERT_EQ(rc, NET_INTEREST_OK);
    ASSERT_EQ(result.count, 0);
}

/**
 * NULL config/result/viewpoint returns error.
 */
TEST(test_null_safety) {
    net_interest_entity_t entities[1];
    entities[0] = make_ent(0, 10.0f, 0.0f, 0.0f, 26, 1);

    float viewpoint[3] = {0.0f, 0.0f, 0.0f};
    net_interest_config_t cfg = { .radius = 100.0f, .budget_bytes = 1000 };

    uint16_t ids_out[4];
    net_interest_result_t result = {
        .entity_ids = ids_out, .capacity = 4, .count = 0
    };

    ASSERT_EQ(net_interest_query(entities, 1, NULL, &cfg, &result),
              NET_INTEREST_ERR_INVALID);
    ASSERT_EQ(net_interest_query(entities, 1, viewpoint, NULL, &result),
              NET_INTEREST_ERR_INVALID);
    ASSERT_EQ(net_interest_query(entities, 1, viewpoint, &cfg, NULL),
              NET_INTEREST_ERR_INVALID);
}

/* ── Runner ─────────────────────────────────────────────────────── */

int main(void) {
    printf("p007_net_interest_tests:\n");
    RUN(test_entities_within_radius);
    RUN(test_entities_outside_radius);
    RUN(test_viewpoint_movement);
    RUN(test_budget_caps_output);
    RUN(test_priority_by_distance);
    RUN(test_dirty_only_filtering);
    RUN(test_edge_cases);
    RUN(test_null_safety);
    printf("%d/%d tests passed\n", g_pass_count,
           g_pass_count + g_fail_count);
    return g_fail_count ? 1 : 0;
}
