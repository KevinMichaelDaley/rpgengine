/**
 * @file aegis_event_tests.c
 * @brief Tests for Aegis event queue and topic subscription system.
 *
 * Per ref/aegis_bytecode_spec.md §2.1, §2.2.
 * Tests cover: event queue push/pop, topic subscribe/unsubscribe/publish,
 * queue overflow behavior, multiple subscribers, multiple topics.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/aegis/aegis_event.h"

/* ======================================================================= */
/* Test harness                                                             */
/* ======================================================================= */

static int g_pass;
static int g_fail;

#define ASSERT(cond)                                                         \
    do {                                                                     \
        if (!(cond)) {                                                       \
            printf("  ASSERT FAILED: %s (line %d)\n", #cond, __LINE__);      \
            return false;                                                    \
        }                                                                    \
    } while (0)

#define ASSERT_INT_EQ(expected, actual)                                      \
    do {                                                                     \
        int _e = (expected), _a = (actual);                                  \
        if (_e != _a) {                                                      \
            printf("  ASSERT FAILED: %d != %d (line %d)\n", _e, _a,         \
                   __LINE__);                                                \
            return false;                                                    \
        }                                                                    \
    } while (0)

#define ASSERT_U32_EQ(expected, actual)                                      \
    do {                                                                     \
        uint32_t _e = (expected), _a = (actual);                             \
        if (_e != _a) {                                                      \
            printf("  ASSERT FAILED: %u != %u (line %d)\n", _e, _a,         \
                   __LINE__);                                                \
            return false;                                                    \
        }                                                                    \
    } while (0)

#define RUN(fn)                                                              \
    do {                                                                     \
        printf("RUN  " #fn "\n");                                            \
        if (fn()) { g_pass++; printf("OK   " #fn "\n"); }                    \
        else      { g_fail++; printf("FAIL " #fn "\n"); }                    \
    } while (0)

/* ======================================================================= */
/* Helpers                                                                  */
/* ======================================================================= */

/** Build a simple event with type hash, source entity, tick, and no payload. */
static aegis_event_t make_event(uint32_t type_hash, uint32_t source,
                                uint32_t tick) {
    aegis_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type        = type_hash;
    ev.source      = source;
    ev.tick        = tick;
    ev.payload_len = 0;
    return ev;
}

/** Build an event with a small payload (up to 4 bytes). */
static aegis_event_t make_event_with_payload(uint32_t type_hash,
                                             uint32_t source,
                                             uint32_t tick,
                                             const void *data,
                                             uint32_t len) {
    aegis_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type        = type_hash;
    ev.source      = source;
    ev.tick        = tick;
    ev.payload_len = len;
    if (len > 0 && len <= AEGIS_EVENT_MAX_PAYLOAD) {
        memcpy(ev.payload, data, len);
    }
    return ev;
}

/* ======================================================================= */
/* Test: Queue init, push, pop round-trip                                   */
/* ======================================================================= */

static bool test_queue_push_pop(void) {
    aegis_event_queue_t q;
    aegis_event_queue_init(&q, 8);

    aegis_event_t ev = make_event(0x1234, 42, 100);
    ASSERT(aegis_event_queue_push(&q, &ev));
    ASSERT_INT_EQ(1, (int)aegis_event_queue_count(&q));

    aegis_event_t out;
    ASSERT(aegis_event_queue_pop(&q, &out));
    ASSERT_U32_EQ(0x1234, out.type);
    ASSERT_U32_EQ(42, out.source);
    ASSERT_U32_EQ(100, out.tick);
    ASSERT_INT_EQ(0, (int)aegis_event_queue_count(&q));

    aegis_event_queue_destroy(&q);
    return true;
}

/* ======================================================================= */
/* Test: Pop from empty queue returns false                                 */
/* ======================================================================= */

static bool test_pop_empty(void) {
    aegis_event_queue_t q;
    aegis_event_queue_init(&q, 4);

    aegis_event_t out;
    ASSERT(!aegis_event_queue_pop(&q, &out));
    ASSERT_INT_EQ(0, (int)aegis_event_queue_count(&q));

    aegis_event_queue_destroy(&q);
    return true;
}

/* ======================================================================= */
/* Test: Queue FIFO ordering                                                */
/* ======================================================================= */

static bool test_queue_fifo_order(void) {
    aegis_event_queue_t q;
    aegis_event_queue_init(&q, 8);

    for (uint32_t i = 0; i < 5; i++) {
        aegis_event_t ev = make_event(i, i + 100, i + 200);
        ASSERT(aegis_event_queue_push(&q, &ev));
    }
    ASSERT_INT_EQ(5, (int)aegis_event_queue_count(&q));

    for (uint32_t i = 0; i < 5; i++) {
        aegis_event_t out;
        ASSERT(aegis_event_queue_pop(&q, &out));
        ASSERT_U32_EQ(i, out.type);
        ASSERT_U32_EQ(i + 100, out.source);
        ASSERT_U32_EQ(i + 200, out.tick);
    }
    ASSERT_INT_EQ(0, (int)aegis_event_queue_count(&q));

    aegis_event_queue_destroy(&q);
    return true;
}

/* ======================================================================= */
/* Test: Queue overflow drops oldest events                                 */
/* ======================================================================= */

static bool test_queue_overflow_drops_oldest(void) {
    aegis_event_queue_t q;
    aegis_event_queue_init(&q, 4);

    /* Fill queue to capacity. */
    for (uint32_t i = 0; i < 4; i++) {
        aegis_event_t ev = make_event(i, 0, i);
        ASSERT(aegis_event_queue_push(&q, &ev));
    }
    ASSERT_INT_EQ(4, (int)aegis_event_queue_count(&q));

    /* Push 2 more → should drop events 0 and 1. */
    for (uint32_t i = 4; i < 6; i++) {
        aegis_event_t ev = make_event(i, 0, i);
        ASSERT(aegis_event_queue_push(&q, &ev));
    }
    /* Count stays at capacity. */
    ASSERT_INT_EQ(4, (int)aegis_event_queue_count(&q));

    /* Pop should return events 2, 3, 4, 5 (oldest dropped). */
    for (uint32_t expected = 2; expected < 6; expected++) {
        aegis_event_t out;
        ASSERT(aegis_event_queue_pop(&q, &out));
        ASSERT_U32_EQ(expected, out.type);
    }

    aegis_event_queue_destroy(&q);
    return true;
}

/* ======================================================================= */
/* Test: Event with payload round-trip                                      */
/* ======================================================================= */

static bool test_event_payload(void) {
    aegis_event_queue_t q;
    aegis_event_queue_init(&q, 4);

    uint32_t data = 0xDEADBEEF;
    aegis_event_t ev = make_event_with_payload(0xABCD, 7, 42,
                                               &data, sizeof(data));
    ASSERT(aegis_event_queue_push(&q, &ev));

    aegis_event_t out;
    ASSERT(aegis_event_queue_pop(&q, &out));
    ASSERT_U32_EQ(0xABCD, out.type);
    ASSERT_U32_EQ(4, out.payload_len);
    uint32_t got;
    memcpy(&got, out.payload, sizeof(got));
    ASSERT_U32_EQ(0xDEADBEEF, got);

    aegis_event_queue_destroy(&q);
    return true;
}

/* ======================================================================= */
/* Test: Topic hash from string                                             */
/* ======================================================================= */

static bool test_topic_hash(void) {
    uint32_t h1 = aegis_topic_hash("!hit");
    uint32_t h2 = aegis_topic_hash("!behave");
    uint32_t h3 = aegis_topic_hash("!hit");

    /* Same name → same hash. */
    ASSERT_U32_EQ(h1, h3);
    /* Different names → different hashes (with high probability). */
    ASSERT(h1 != h2);
    /* Non-zero. */
    ASSERT(h1 != 0);
    ASSERT(h2 != 0);

    return true;
}

/* ======================================================================= */
/* Test: Subscribe + publish + pop round-trip                               */
/* ======================================================================= */

static bool test_subscribe_publish_pop(void) {
    aegis_topic_table_t table;
    aegis_topic_table_init(&table, 16, 4);

    uint32_t hit_hash = aegis_topic_hash("!hit");

    /* Create a queue for script 0. */
    aegis_event_queue_t queues[4];
    for (int i = 0; i < 4; i++) {
        aegis_event_queue_init(&queues[i], 8);
    }

    /* Subscribe script 0 to !hit. */
    ASSERT(aegis_topic_subscribe(&table, hit_hash, 0));

    /* Publish a !hit event. */
    aegis_event_t ev = make_event(hit_hash, 99, 500);
    aegis_topic_publish(&table, &ev, queues, 4);

    /* Script 0 should have received it. */
    aegis_event_t out;
    ASSERT(aegis_event_queue_pop(&queues[0], &out));
    ASSERT_U32_EQ(hit_hash, out.type);
    ASSERT_U32_EQ(99, out.source);
    ASSERT_U32_EQ(500, out.tick);

    /* Script 1 should NOT have it. */
    ASSERT(!aegis_event_queue_pop(&queues[1], &out));

    for (int i = 0; i < 4; i++) {
        aegis_event_queue_destroy(&queues[i]);
    }
    aegis_topic_table_destroy(&table);
    return true;
}

/* ======================================================================= */
/* Test: Multiple subscribers to same topic                                 */
/* ======================================================================= */

static bool test_multiple_subscribers(void) {
    aegis_topic_table_t table;
    aegis_topic_table_init(&table, 16, 8);

    uint32_t hit_hash = aegis_topic_hash("!hit");

    aegis_event_queue_t queues[4];
    for (int i = 0; i < 4; i++) {
        aegis_event_queue_init(&queues[i], 8);
    }

    /* Subscribe scripts 0, 1, 2 to !hit. */
    ASSERT(aegis_topic_subscribe(&table, hit_hash, 0));
    ASSERT(aegis_topic_subscribe(&table, hit_hash, 1));
    ASSERT(aegis_topic_subscribe(&table, hit_hash, 2));

    /* Publish. */
    aegis_event_t ev = make_event(hit_hash, 10, 1);
    aegis_topic_publish(&table, &ev, queues, 4);

    /* All three should receive it. */
    for (int i = 0; i < 3; i++) {
        aegis_event_t out;
        ASSERT(aegis_event_queue_pop(&queues[i], &out));
        ASSERT_U32_EQ(hit_hash, out.type);
        ASSERT_U32_EQ(10, out.source);
    }
    /* Script 3 should not. */
    aegis_event_t out;
    ASSERT(!aegis_event_queue_pop(&queues[3], &out));

    for (int i = 0; i < 4; i++) {
        aegis_event_queue_destroy(&queues[i]);
    }
    aegis_topic_table_destroy(&table);
    return true;
}

/* ======================================================================= */
/* Test: Subscribe to multiple topics                                       */
/* ======================================================================= */

static bool test_multiple_topics(void) {
    aegis_topic_table_t table;
    aegis_topic_table_init(&table, 16, 8);

    uint32_t hit_hash    = aegis_topic_hash("!hit");
    uint32_t behave_hash = aegis_topic_hash("!behave");

    aegis_event_queue_t queues[2];
    aegis_event_queue_init(&queues[0], 8);
    aegis_event_queue_init(&queues[1], 8);

    /* Script 0 subscribes to both topics. */
    ASSERT(aegis_topic_subscribe(&table, hit_hash, 0));
    ASSERT(aegis_topic_subscribe(&table, behave_hash, 0));
    /* Script 1 subscribes only to !behave. */
    ASSERT(aegis_topic_subscribe(&table, behave_hash, 1));

    /* Publish !hit event. */
    aegis_event_t ev1 = make_event(hit_hash, 1, 10);
    aegis_topic_publish(&table, &ev1, queues, 2);

    /* Publish !behave event. */
    aegis_event_t ev2 = make_event(behave_hash, 2, 20);
    aegis_topic_publish(&table, &ev2, queues, 2);

    /* Script 0: should have both events. */
    aegis_event_t out;
    ASSERT(aegis_event_queue_pop(&queues[0], &out));
    ASSERT_U32_EQ(hit_hash, out.type);
    ASSERT(aegis_event_queue_pop(&queues[0], &out));
    ASSERT_U32_EQ(behave_hash, out.type);

    /* Script 1: should have only !behave. */
    ASSERT(aegis_event_queue_pop(&queues[1], &out));
    ASSERT_U32_EQ(behave_hash, out.type);
    ASSERT(!aegis_event_queue_pop(&queues[1], &out));

    aegis_event_queue_destroy(&queues[0]);
    aegis_event_queue_destroy(&queues[1]);
    aegis_topic_table_destroy(&table);
    return true;
}

/* ======================================================================= */
/* Test: Unsubscribe removes routing                                        */
/* ======================================================================= */

static bool test_unsubscribe(void) {
    aegis_topic_table_t table;
    aegis_topic_table_init(&table, 16, 8);

    uint32_t hit_hash = aegis_topic_hash("!hit");

    aegis_event_queue_t queues[2];
    aegis_event_queue_init(&queues[0], 8);
    aegis_event_queue_init(&queues[1], 8);

    ASSERT(aegis_topic_subscribe(&table, hit_hash, 0));
    ASSERT(aegis_topic_subscribe(&table, hit_hash, 1));

    /* Unsubscribe script 0. */
    ASSERT(aegis_topic_unsubscribe(&table, hit_hash, 0));

    /* Publish. */
    aegis_event_t ev = make_event(hit_hash, 5, 50);
    aegis_topic_publish(&table, &ev, queues, 2);

    /* Script 0 should NOT receive it. */
    aegis_event_t out;
    ASSERT(!aegis_event_queue_pop(&queues[0], &out));
    /* Script 1 should. */
    ASSERT(aegis_event_queue_pop(&queues[1], &out));
    ASSERT_U32_EQ(hit_hash, out.type);

    aegis_event_queue_destroy(&queues[0]);
    aegis_event_queue_destroy(&queues[1]);
    aegis_topic_table_destroy(&table);
    return true;
}

/* ======================================================================= */
/* Test: Publish with no subscribers is a no-op                             */
/* ======================================================================= */

static bool test_publish_no_subscribers(void) {
    aegis_topic_table_t table;
    aegis_topic_table_init(&table, 16, 8);

    aegis_event_queue_t queues[1];
    aegis_event_queue_init(&queues[0], 4);

    /* Publish to a topic nobody subscribed to. */
    uint32_t hit_hash = aegis_topic_hash("!hit");
    aegis_event_t ev = make_event(hit_hash, 1, 1);
    aegis_topic_publish(&table, &ev, queues, 1);

    /* Queue should be empty. */
    aegis_event_t out;
    ASSERT(!aegis_event_queue_pop(&queues[0], &out));

    aegis_event_queue_destroy(&queues[0]);
    aegis_topic_table_destroy(&table);
    return true;
}

/* ======================================================================= */
/* Test: Duplicate subscribe is rejected                                    */
/* ======================================================================= */

static bool test_duplicate_subscribe(void) {
    aegis_topic_table_t table;
    aegis_topic_table_init(&table, 16, 8);

    uint32_t hit_hash = aegis_topic_hash("!hit");
    ASSERT(aegis_topic_subscribe(&table, hit_hash, 0));
    /* Second subscribe of same script to same topic should fail. */
    ASSERT(!aegis_topic_subscribe(&table, hit_hash, 0));

    aegis_topic_table_destroy(&table);
    return true;
}

/* ======================================================================= */
/* Test: Unsubscribe from non-subscribed topic returns false                */
/* ======================================================================= */

static bool test_unsubscribe_not_subscribed(void) {
    aegis_topic_table_t table;
    aegis_topic_table_init(&table, 16, 8);

    uint32_t hit_hash = aegis_topic_hash("!hit");
    /* Never subscribed. */
    ASSERT(!aegis_topic_unsubscribe(&table, hit_hash, 0));

    aegis_topic_table_destroy(&table);
    return true;
}

/* ======================================================================= */
/* Test: Queue wraparound (push-pop-push cycle)                             */
/* ======================================================================= */

static bool test_queue_wraparound(void) {
    aegis_event_queue_t q;
    aegis_event_queue_init(&q, 4);

    /* Fill, drain, then fill again to exercise wraparound. */
    for (uint32_t cycle = 0; cycle < 3; cycle++) {
        for (uint32_t i = 0; i < 4; i++) {
            uint32_t val = cycle * 100 + i;
            aegis_event_t ev = make_event(val, 0, 0);
            ASSERT(aegis_event_queue_push(&q, &ev));
        }
        for (uint32_t i = 0; i < 4; i++) {
            uint32_t val = cycle * 100 + i;
            aegis_event_t out;
            ASSERT(aegis_event_queue_pop(&q, &out));
            ASSERT_U32_EQ(val, out.type);
        }
        ASSERT_INT_EQ(0, (int)aegis_event_queue_count(&q));
    }

    aegis_event_queue_destroy(&q);
    return true;
}

/* ======================================================================= */
/* Test: Many topics in table                                               */
/* ======================================================================= */

static bool test_many_topics(void) {
    aegis_topic_table_t table;
    aegis_topic_table_init(&table, 64, 16);

    aegis_event_queue_t queues[1];
    aegis_event_queue_init(&queues[0], 64);

    /* Subscribe script 0 to 32 different topics. */
    char topic_name[32];
    uint32_t hashes[32];
    for (int i = 0; i < 32; i++) {
        snprintf(topic_name, sizeof(topic_name), "!topic_%d", i);
        hashes[i] = aegis_topic_hash(topic_name);
        ASSERT(aegis_topic_subscribe(&table, hashes[i], 0));
    }

    /* Publish to each topic. */
    for (int i = 0; i < 32; i++) {
        aegis_event_t ev = make_event(hashes[i], (uint32_t)i, 0);
        aegis_topic_publish(&table, &ev, queues, 1);
    }

    /* Pop all 32 events. */
    for (int i = 0; i < 32; i++) {
        aegis_event_t out;
        ASSERT(aegis_event_queue_pop(&queues[0], &out));
        ASSERT_U32_EQ(hashes[i], out.type);
    }

    aegis_event_queue_destroy(&queues[0]);
    aegis_topic_table_destroy(&table);
    return true;
}

/* ======================================================================= */
/* Main                                                                     */
/* ======================================================================= */

int main(void) {
    printf("=== Aegis Event Queue Tests ===\n\n");

    /* Queue tests */
    RUN(test_queue_push_pop);
    RUN(test_pop_empty);
    RUN(test_queue_fifo_order);
    RUN(test_queue_overflow_drops_oldest);
    RUN(test_event_payload);
    RUN(test_queue_wraparound);

    /* Topic/subscribe tests */
    RUN(test_topic_hash);
    RUN(test_subscribe_publish_pop);
    RUN(test_multiple_subscribers);
    RUN(test_multiple_topics);
    RUN(test_unsubscribe);
    RUN(test_publish_no_subscribers);
    RUN(test_duplicate_subscribe);
    RUN(test_unsubscribe_not_subscribed);
    RUN(test_many_topics);

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
