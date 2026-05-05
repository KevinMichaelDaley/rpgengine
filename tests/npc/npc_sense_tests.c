/**
 * @file npc_sense_tests.c
 * @brief Auto-sense pipeline tests: awareness tracking, KG insertion.
 *
 * Covers:
 * - New entity detected → inserted into KG + awareness list
 * - Known entity seen again → salience updated, last_seen refreshed
 * - Lost entity → salience decayed, still in awareness
 * - Long-lost entity → removed from awareness
 * - Empty result → awareness unchanged
 */

#include "ferrum/npc/npc_knowledge_graph.h"
#include "ferrum/npc/npc_sense.h"
#include "ferrum/aegis/aegis_sense.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RUN(fn) do { printf("  %-48s ", #fn); fn(); } while (0)
#define ASSERT_TRUE(expr) do { \
    if (!(expr)) { \
        printf("FAIL (%s:%d)\n", __FILE__, __LINE__); \
        g_fail++; \
        return; \
    } \
} while (0)
#define ASSERT_INT_EQ(exp, act) do { \
    if ((exp) != (act)) { \
        printf("FAIL (%s:%d) expected %d got %d\n", \
               __FILE__, __LINE__, (int)(exp), (int)(act)); \
        g_fail++; \
        return; \
    } \
} while (0)
#define ASSERT_FLOAT_NEAR(exp, act, tol) do { \
    if (fabsf((exp) - (act)) > (tol)) { \
        printf("FAIL (%s:%d) expected %.6f got %.6f\n", \
               __FILE__, __LINE__, (float)(exp), (float)(act)); \
        g_fail++; \
        return; \
    } \
} while (0)
#define PASS() do { printf("PASS\n"); g_pass++; } while (0)

static int g_pass = 0;
static int g_fail = 0;

/* Build a mock sense result in a pre-allocated buffer. */
static uint32_t build_sense_result(uint8_t *buf, uint32_t buf_sz,
                                   uint32_t *entity_ids,
                                   float *distances,
                                   float *saliences,
                                   uint32_t count) {
    aegis_sense_result_t *hdr = (aegis_sense_result_t *)buf;
    memset(buf, 0, buf_sz);
    hdr->status = 0;
    hdr->entity_count = count;
    hdr->event_count = 0;
    hdr->total_found = count;
    hdr->truncated = 0;

    uint8_t *write = buf + sizeof(aegis_sense_result_t);
    for (uint32_t i = 0; i < count; i++) {
        aegis_sense_entity_t *ent = (aegis_sense_entity_t *)write;
        ent->entity_id = entity_ids[i];
        ent->distance = distances[i];
        ent->salience = saliences[i];
        ent->flags = AEGIS_SENSE_ENTITY_VISIBLE;
        ent->name[0] = '\0';
        write += aegis_sense_entity_size(ent->name);
    }
    return (uint32_t)(write - buf);
}

/* Build a mock sense result with named entities. names must be count char* strings. */
static uint32_t build_sense_result_named(uint8_t *buf, uint32_t buf_sz,
                                         uint32_t *entity_ids,
                                         float *distances,
                                         float *saliences,
                                         const char **names,
                                         uint32_t count) {
    aegis_sense_result_t *hdr = (aegis_sense_result_t *)buf;
    memset(buf, 0, buf_sz);
    hdr->status = 0;
    hdr->entity_count = count;
    hdr->event_count = 0;
    hdr->total_found = count;
    hdr->truncated = 0;

    uint8_t *write = buf + sizeof(aegis_sense_result_t);
    for (uint32_t i = 0; i < count; i++) {
        aegis_sense_entity_t *ent = (aegis_sense_entity_t *)write;
        uint32_t name_len = (uint32_t)strlen(names[i]);
        ent->entity_id = entity_ids[i];
        ent->distance = distances[i];
        ent->salience = saliences[i];
        ent->flags = AEGIS_SENSE_ENTITY_VISIBLE;
        memcpy(ent->name, names[i], name_len + 1);
        write += aegis_sense_entity_size(ent->name);
    }
    return (uint32_t)(write - buf);
}

/* ------------------------------------------------------------------ */
/* Tests                                                              */
/* ------------------------------------------------------------------ */

static void test_awareness_init(void) {
    npc_sense_awareness_t aw;
    npc_sense_awareness_init(&aw, 16);
    ASSERT_INT_EQ(0, aw.count);
    ASSERT_INT_EQ(16, aw.cap);
    ASSERT_TRUE(aw.entries != NULL);
    npc_sense_awareness_destroy(&aw);
    PASS();
}

static void test_awareness_new_entity(void) {
    npc_sense_awareness_t aw;
    npc_sense_awareness_init(&aw, 8);

    uint8_t buf[512];
    uint32_t ids[] = {42};
    float dists[] = {10.0f};
    float sals[] = {0.9f};
    build_sense_result(buf, sizeof(buf), ids, dists, sals, 1);

    aegis_sense_result_t *res = (aegis_sense_result_t *)buf;
    npc_knowledge_graph_t kg;
    npc_kg_init(&kg, 16, 4);

    uint32_t inserted = npc_sense_auto_update(&aw, &kg, res, 1000000);
    ASSERT_INT_EQ(1, inserted);
    ASSERT_INT_EQ(1, aw.count);

    const npc_sense_awareness_entry_t *e = npc_sense_awareness_find(&aw, 42);
    ASSERT_TRUE(e != NULL);
    ASSERT_INT_EQ(42, e->entity_id);
    ASSERT_FLOAT_NEAR(0.9f, e->last_salience, 0.01f);
    ASSERT_INT_EQ(1000000, e->last_seen_us);

    npc_kg_destroy(&kg);
    npc_sense_awareness_destroy(&aw);
    PASS();
}

static void test_awareness_known_entity_refresh(void) {
    npc_sense_awareness_t aw;
    npc_sense_awareness_init(&aw, 8);

    uint8_t buf[512];
    uint32_t ids[] = {42};
    float dists[] = {5.0f};
    float sals[] = {0.8f};
    build_sense_result(buf, sizeof(buf), ids, dists, sals, 1);

    aegis_sense_result_t *res = (aegis_sense_result_t *)buf;
    npc_knowledge_graph_t kg;
    npc_kg_init(&kg, 16, 4);

    npc_sense_auto_update(&aw, &kg, res, 1000000);

    /* Same entity seen again with different salience. */
    sals[0] = 0.6f;
    build_sense_result(buf, sizeof(buf), ids, dists, sals, 1);
    uint32_t inserted = npc_sense_auto_update(&aw, &kg, res, 2000000);
    ASSERT_INT_EQ(0, inserted); /* no new insertion */
    ASSERT_INT_EQ(1, aw.count);

    const npc_sense_awareness_entry_t *e = npc_sense_awareness_find(&aw, 42);
    ASSERT_TRUE(e != NULL);
    ASSERT_FLOAT_NEAR(0.6f, e->last_salience, 0.01f);
    ASSERT_INT_EQ(2000000, e->last_seen_us);

    npc_kg_destroy(&kg);
    npc_sense_awareness_destroy(&aw);
    PASS();
}

static void test_awareness_lost_entity_decay(void) {
    npc_sense_awareness_t aw;
    npc_sense_awareness_init(&aw, 8);

    uint8_t buf[512];
    uint32_t ids[] = {10, 20};
    float dists[] = {1.0f, 2.0f};
    float sals[] = {1.0f, 0.5f};
    build_sense_result(buf, sizeof(buf), ids, dists, sals, 2);

    aegis_sense_result_t *res = (aegis_sense_result_t *)buf;
    npc_knowledge_graph_t kg;
    npc_kg_init(&kg, 16, 4);

    npc_sense_auto_update(&aw, &kg, res, 1000000);
    ASSERT_INT_EQ(2, aw.count);

    /* Only entity 10 appears in the next sweep; entity 20 is lost. */
    build_sense_result(buf, sizeof(buf), ids, dists, sals, 1);
    npc_sense_auto_update(&aw, &kg, res, 2000000);

    ASSERT_INT_EQ(2, aw.count); /* lost entity still in awareness */
    const npc_sense_awareness_entry_t *e20 = npc_sense_awareness_find(&aw, 20);
    ASSERT_TRUE(e20 != NULL);
    /* Lost entity's salience decays toward zero. */
    ASSERT_FLOAT_NEAR(1.0f / NPC_SENSE_SALIENCE_DECAY_FACTOR * 0.5f,
                       e20->last_salience, 0.1f);

    /* Entity 10 is still fresh. */
    const npc_sense_awareness_entry_t *e10 = npc_sense_awareness_find(&aw, 10);
    ASSERT_TRUE(e10 != NULL);
    ASSERT_FLOAT_NEAR(1.0f, e10->last_salience, 0.01f);

    npc_kg_destroy(&kg);
    npc_sense_awareness_destroy(&aw);
    PASS();
}

static void test_awareness_long_lost_removal(void) {
    npc_sense_awareness_t aw;
    npc_sense_awareness_init(&aw, 8);

    uint8_t buf[512];
    uint32_t ids[] = {10};
    float dists[] = {1.0f};
    float sals[] = {0.1f};
    build_sense_result(buf, sizeof(buf), ids, dists, sals, 1);

    aegis_sense_result_t *res = (aegis_sense_result_t *)buf;
    npc_knowledge_graph_t kg;
    npc_kg_init(&kg, 16, 4);

    npc_sense_auto_update(&aw, &kg, res, 1000000);
    ASSERT_INT_EQ(1, aw.count);

    /* Many sweeps with empty result: entity repeatedly decays. */
    aegis_sense_result_t empty;
    memset(&empty, 0, sizeof(empty));
    empty.status = 0;
    for (int i = 0; i < 20; i++) {
        npc_sense_auto_update(&aw, &kg, &empty, 2000000 + (uint64_t)i * 1000000);
    }

    /* Entity should be removed once salience drops to 0. */
    const npc_sense_awareness_entry_t *e10 = npc_sense_awareness_find(&aw, 10);
    ASSERT_TRUE(e10 == NULL);
    ASSERT_INT_EQ(0, aw.count);

    npc_kg_destroy(&kg);
    npc_sense_awareness_destroy(&aw);
    PASS();
}

static void test_awareness_multiple_new_entities(void) {
    npc_sense_awareness_t aw;
    npc_sense_awareness_init(&aw, 8);

    uint8_t buf[512];
    uint32_t ids[] = {100, 200, 300};
    float dists[] = {5.0f, 10.0f, 15.0f};
    float sals[] = {9.0f / 9.0f, 1.0f, 0.5f};
    build_sense_result(buf, sizeof(buf), ids, dists, sals, 3);

    aegis_sense_result_t *res = (aegis_sense_result_t *)buf;
    npc_knowledge_graph_t kg;
    npc_kg_init(&kg, 16, 4);

    uint32_t inserted = npc_sense_auto_update(&aw, &kg, res, 1000000);
    ASSERT_INT_EQ(3, inserted);
    ASSERT_INT_EQ(3, aw.count);
    ASSERT_INT_EQ(3, kg.node_count);

    npc_kg_destroy(&kg);
    npc_sense_awareness_destroy(&aw);
    PASS();
}

static void test_awareness_empty_result(void) {
    npc_sense_awareness_t aw;
    npc_sense_awareness_init(&aw, 8);

    npc_knowledge_graph_t kg;
    npc_kg_init(&kg, 16, 4);

    aegis_sense_result_t empty;
    memset(&empty, 0, sizeof(empty));
    empty.status = 0;

    uint32_t inserted = npc_sense_auto_update(&aw, &kg, &empty, 1000000);
    ASSERT_INT_EQ(0, inserted);
    ASSERT_INT_EQ(0, aw.count);

    npc_kg_destroy(&kg);
    npc_sense_awareness_destroy(&aw);
    PASS();
}

static void test_awareness_find_nonexistent(void) {
    npc_sense_awareness_t aw;
    npc_sense_awareness_init(&aw, 8);

    const npc_sense_awareness_entry_t *e = npc_sense_awareness_find(&aw, 999);
    ASSERT_TRUE(e == NULL);

    npc_sense_awareness_destroy(&aw);
    PASS();
}

static void test_awareness_named_entities_varying_lengths(void) {
    npc_sense_awareness_t aw;
    npc_sense_awareness_init(&aw, 8);

    uint8_t buf[512];
    uint32_t ids[] = {10, 20, 30};
    float dists[] = {1.0f, 2.0f, 3.0f};
    float sals[] = {1.0f, 0.8f, 0.6f};
    const char *names[] = {"Orc", "DragonLord", "Z"};
    build_sense_result_named(buf, sizeof(buf), ids, dists, sals, names, 3);

    aegis_sense_result_t *res = (aegis_sense_result_t *)buf;
    npc_knowledge_graph_t kg;
    npc_kg_init(&kg, 16, 4);

    uint32_t inserted = npc_sense_auto_update(&aw, &kg, res, 1000000);
    ASSERT_INT_EQ(3, inserted);
    ASSERT_INT_EQ(3, aw.count);

    const npc_sense_awareness_entry_t *e;
    e = npc_sense_awareness_find(&aw, 10);
    ASSERT_TRUE(e != NULL);
    ASSERT_FLOAT_NEAR(1.0f, e->last_salience, 0.01f);

    e = npc_sense_awareness_find(&aw, 20);
    ASSERT_TRUE(e != NULL);
    ASSERT_FLOAT_NEAR(0.8f, e->last_salience, 0.01f);

    e = npc_sense_awareness_find(&aw, 30);
    ASSERT_TRUE(e != NULL);
    ASSERT_FLOAT_NEAR(0.6f, e->last_salience, 0.01f);

    npc_kg_destroy(&kg);
    npc_sense_awareness_destroy(&aw);
    PASS();
}

static void test_awareness_named_entities_mixed_empty(void) {
    npc_sense_awareness_t aw;
    npc_sense_awareness_init(&aw, 8);

    uint8_t buf[512];
    uint32_t ids[] = {1, 2, 3, 4};
    float dists[] = {5.0f, 10.0f, 15.0f, 20.0f};
    float sals[] = {0.9f, 0.7f, 0.5f, 0.3f};
    const char *names[] = {"", "Goblin", "", "ElderLichKing"};
    build_sense_result_named(buf, sizeof(buf), ids, dists, sals, names, 4);

    aegis_sense_result_t *res = (aegis_sense_result_t *)buf;
    npc_knowledge_graph_t kg;
    npc_kg_init(&kg, 16, 4);

    uint32_t inserted = npc_sense_auto_update(&aw, &kg, res, 1000000);
    ASSERT_INT_EQ(4, inserted);
    ASSERT_INT_EQ(4, aw.count);

    const npc_sense_awareness_entry_t *e;
    e = npc_sense_awareness_find(&aw, 1);
    ASSERT_TRUE(e != NULL);
    ASSERT_FLOAT_NEAR(0.9f, e->last_salience, 0.01f);

    e = npc_sense_awareness_find(&aw, 2);
    ASSERT_TRUE(e != NULL);
    ASSERT_FLOAT_NEAR(0.7f, e->last_salience, 0.01f);

    e = npc_sense_awareness_find(&aw, 3);
    ASSERT_TRUE(e != NULL);
    ASSERT_FLOAT_NEAR(0.5f, e->last_salience, 0.01f);

    e = npc_sense_awareness_find(&aw, 4);
    ASSERT_TRUE(e != NULL);
    ASSERT_FLOAT_NEAR(0.3f, e->last_salience, 0.01f);

    npc_kg_destroy(&kg);
    npc_sense_awareness_destroy(&aw);
    PASS();
}

static void test_awareness_named_entity_known_refresh(void) {
    npc_sense_awareness_t aw;
    npc_sense_awareness_init(&aw, 8);

    uint8_t buf[512];
    uint32_t ids[] = {42};
    float dists[] = {5.0f};
    float sals[] = {0.8f};
    const char *names[] = {"Merchant"};
    build_sense_result_named(buf, sizeof(buf), ids, dists, sals, names, 1);

    aegis_sense_result_t *res = (aegis_sense_result_t *)buf;
    npc_knowledge_graph_t kg;
    npc_kg_init(&kg, 16, 4);

    npc_sense_auto_update(&aw, &kg, res, 1000000);

    /* Same named entity seen again. */
    sals[0] = 0.6f;
    build_sense_result_named(buf, sizeof(buf), ids, dists, sals, names, 1);
    uint32_t inserted = npc_sense_auto_update(&aw, &kg, res, 2000000);
    ASSERT_INT_EQ(0, inserted);
    ASSERT_INT_EQ(1, aw.count);

    const npc_sense_awareness_entry_t *e = npc_sense_awareness_find(&aw, 42);
    ASSERT_TRUE(e != NULL);
    ASSERT_FLOAT_NEAR(0.6f, e->last_salience, 0.01f);
    ASSERT_INT_EQ(2000000, e->last_seen_us);

    npc_kg_destroy(&kg);
    npc_sense_awareness_destroy(&aw);
    PASS();
}

static void test_awareness_named_and_unnamed_decay(void) {
    npc_sense_awareness_t aw;
    npc_sense_awareness_init(&aw, 8);

    uint8_t buf[512];
    uint32_t ids[] = {10, 20};
    float dists[] = {1.0f, 2.0f};
    float sals[] = {1.0f, 0.5f};
    const char *names[] = {"Bear", "AncientRedDragon"};
    build_sense_result_named(buf, sizeof(buf), ids, dists, sals, names, 2);

    aegis_sense_result_t *res = (aegis_sense_result_t *)buf;
    npc_knowledge_graph_t kg;
    npc_kg_init(&kg, 16, 4);

    npc_sense_auto_update(&aw, &kg, res, 1000000);
    ASSERT_INT_EQ(2, aw.count);

    /* Only entity 10 appears in the next sweep; entity 20 is lost. */
    const char *names_one[] = {"Bear"};
    build_sense_result_named(buf, sizeof(buf), ids, dists, sals, names_one, 1);
    npc_sense_auto_update(&aw, &kg, res, 2000000);

    ASSERT_INT_EQ(2, aw.count);
    const npc_sense_awareness_entry_t *e20 = npc_sense_awareness_find(&aw, 20);
    ASSERT_TRUE(e20 != NULL);
    ASSERT_FLOAT_NEAR(1.0f / NPC_SENSE_SALIENCE_DECAY_FACTOR * 0.5f,
                       e20->last_salience, 0.1f);

    const npc_sense_awareness_entry_t *e10 = npc_sense_awareness_find(&aw, 10);
    ASSERT_TRUE(e10 != NULL);
    ASSERT_FLOAT_NEAR(1.0f, e10->last_salience, 0.01f);

    npc_kg_destroy(&kg);
    npc_sense_awareness_destroy(&aw);
    PASS();
}

/* ── Main ───────────────────────────────────────────────────────── */

int main(void) {
    printf("npc_sense_tests\n");
    RUN(test_awareness_init);
    RUN(test_awareness_new_entity);
    RUN(test_awareness_known_entity_refresh);
    RUN(test_awareness_lost_entity_decay);
    RUN(test_awareness_long_lost_removal);
    RUN(test_awareness_multiple_new_entities);
    RUN(test_awareness_empty_result);
    RUN(test_awareness_find_nonexistent);
    RUN(test_awareness_named_entities_varying_lengths);
    RUN(test_awareness_named_entities_mixed_empty);
    RUN(test_awareness_named_entity_known_refresh);
    RUN(test_awareness_named_and_unnamed_decay);
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
