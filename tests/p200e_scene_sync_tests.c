/**
 * @file p200e_scene_sync_tests.c
 * @brief Unit tests for scene editor sync/persistence module.
 *
 * Tests the offline edit queue, in-flight tracking, save commands,
 * and sync status formatting. Headless — no server required.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/editor/scene/scene_sync.h"

/* ---- Test harness ---- */

#define ASSERT_TRUE(cond)                                                      \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n", __FILE__,      \
                    __LINE__, #cond);                                          \
            return 1;                                                          \
        }                                                                      \
    } while (0)

#define ASSERT_INT_EQ(exp, act)                                                \
    do {                                                                        \
        if ((exp) != (act)) {                                                  \
            fprintf(stderr, "ASSERT_INT_EQ failed: %s:%d: expected %d got "   \
                    "%d\n", __FILE__, __LINE__, (int)(exp), (int)(act));        \
            return 1;                                                          \
        }                                                                      \
    } while (0)

#define ASSERT_STR_EQ(exp, act)                                                \
    do {                                                                        \
        if (strcmp((exp), (act)) != 0) {                                        \
            fprintf(stderr, "ASSERT_STR_EQ failed: %s:%d: expected \"%s\" "   \
                    "got \"%s\"\n", __FILE__, __LINE__, (exp), (act));          \
            return 1;                                                          \
        }                                                                      \
    } while (0)

/* ---- Tests ---- */

static int test_sync_init(void) {
    scene_sync_t sync;
    scene_sync_config_t cfg = {0};
    cfg.queue_capacity = 64;

    bool ok = scene_sync_init(&sync, &cfg);
    ASSERT_TRUE(ok);
    ASSERT_INT_EQ(0, (int)sync.in_flight);
    ASSERT_INT_EQ(0, (int)sync.queue_count);
    ASSERT_INT_EQ(SCENE_SYNC_IDLE, (int)sync.state);

    scene_sync_destroy(&sync);
    return 0;
}

static int test_sync_destroy_safe(void) {
    scene_sync_t sync;
    scene_sync_config_t cfg = {0};
    scene_sync_init(&sync, &cfg);
    scene_sync_destroy(&sync);
    scene_sync_destroy(&sync);
    scene_sync_destroy(NULL);
    return 0;
}

static int test_sync_init_defaults(void) {
    scene_sync_t sync;
    bool ok = scene_sync_init(&sync, NULL);
    ASSERT_TRUE(ok);
    ASSERT_TRUE(sync.queue_capacity > 0);
    scene_sync_destroy(&sync);
    return 0;
}

static int test_sync_queue_edit(void) {
    scene_sync_t sync;
    scene_sync_config_t cfg = {0};
    cfg.queue_capacity = 8;
    scene_sync_init(&sync, &cfg);

    bool ok = scene_sync_queue_edit(&sync, "spawn box", 1);
    ASSERT_TRUE(ok);
    ASSERT_INT_EQ(1, (int)sync.queue_count);

    ok = scene_sync_queue_edit(&sync, "delete 42", 2);
    ASSERT_TRUE(ok);
    ASSERT_INT_EQ(2, (int)sync.queue_count);

    scene_sync_destroy(&sync);
    return 0;
}

static int test_sync_dequeue_edit(void) {
    scene_sync_t sync;
    scene_sync_config_t cfg = {0};
    cfg.queue_capacity = 8;
    scene_sync_init(&sync, &cfg);

    scene_sync_queue_edit(&sync, "cmd_a", 1);
    scene_sync_queue_edit(&sync, "cmd_b", 2);

    char buf[256];
    uint32_t cmd_id = 0;
    bool ok = scene_sync_dequeue_edit(&sync, buf, sizeof(buf), &cmd_id);
    ASSERT_TRUE(ok);
    ASSERT_STR_EQ("cmd_a", buf);
    ASSERT_INT_EQ(1, (int)cmd_id);
    ASSERT_INT_EQ(1, (int)sync.queue_count);

    ok = scene_sync_dequeue_edit(&sync, buf, sizeof(buf), &cmd_id);
    ASSERT_TRUE(ok);
    ASSERT_STR_EQ("cmd_b", buf);
    ASSERT_INT_EQ(2, (int)cmd_id);
    ASSERT_INT_EQ(0, (int)sync.queue_count);

    /* Empty queue returns false */
    ok = scene_sync_dequeue_edit(&sync, buf, sizeof(buf), &cmd_id);
    ASSERT_TRUE(!ok);

    scene_sync_destroy(&sync);
    return 0;
}

static int test_sync_queue_overflow(void) {
    scene_sync_t sync;
    scene_sync_config_t cfg = {0};
    cfg.queue_capacity = 4;
    scene_sync_init(&sync, &cfg);

    for (int i = 0; i < 4; i++) {
        bool ok = scene_sync_queue_edit(&sync, "cmd", (uint32_t)i);
        ASSERT_TRUE(ok);
    }
    ASSERT_INT_EQ(4, (int)sync.queue_count);

    /* Queue is full — should fail */
    bool ok = scene_sync_queue_edit(&sync, "overflow", 99);
    ASSERT_TRUE(!ok);
    ASSERT_INT_EQ(4, (int)sync.queue_count);

    scene_sync_destroy(&sync);
    return 0;
}

static int test_sync_in_flight_tracking(void) {
    scene_sync_t sync;
    scene_sync_init(&sync, NULL);

    scene_sync_mark_sent(&sync);
    scene_sync_mark_sent(&sync);
    scene_sync_mark_sent(&sync);
    ASSERT_INT_EQ(3, (int)sync.in_flight);

    scene_sync_mark_acked(&sync);
    ASSERT_INT_EQ(2, (int)sync.in_flight);

    scene_sync_mark_acked(&sync);
    scene_sync_mark_acked(&sync);
    ASSERT_INT_EQ(0, (int)sync.in_flight);

    /* Ack below zero should clamp to 0 */
    scene_sync_mark_acked(&sync);
    ASSERT_INT_EQ(0, (int)sync.in_flight);

    scene_sync_destroy(&sync);
    return 0;
}

static int test_sync_state_transitions(void) {
    scene_sync_t sync;
    scene_sync_init(&sync, NULL);

    /* Initially idle */
    ASSERT_INT_EQ(SCENE_SYNC_IDLE, (int)sync.state);

    /* With in-flight commands → syncing */
    scene_sync_mark_sent(&sync);
    scene_sync_update_state(&sync);
    ASSERT_INT_EQ(SCENE_SYNC_SYNCING, (int)sync.state);

    /* All acked → idle */
    scene_sync_mark_acked(&sync);
    scene_sync_update_state(&sync);
    ASSERT_INT_EQ(SCENE_SYNC_IDLE, (int)sync.state);

    /* Offline with queued edits */
    scene_sync_queue_edit(&sync, "cmd", 1);
    sync.state = SCENE_SYNC_OFFLINE;
    scene_sync_update_state(&sync);
    ASSERT_INT_EQ(SCENE_SYNC_OFFLINE, (int)sync.state);

    scene_sync_destroy(&sync);
    return 0;
}

static int test_sync_format_status(void) {
    scene_sync_t sync;
    scene_sync_init(&sync, NULL);
    char buf[128];

    /* Idle */
    scene_sync_format_status(&sync, buf, sizeof(buf));
    ASSERT_TRUE(strstr(buf, "Synced") != NULL);

    /* Syncing */
    scene_sync_mark_sent(&sync);
    scene_sync_mark_sent(&sync);
    scene_sync_update_state(&sync);
    scene_sync_format_status(&sync, buf, sizeof(buf));
    ASSERT_TRUE(strstr(buf, "Syncing") != NULL);
    ASSERT_TRUE(strstr(buf, "2") != NULL);

    /* Offline with queued */
    sync.state = SCENE_SYNC_OFFLINE;
    sync.in_flight = 0;
    scene_sync_queue_edit(&sync, "cmd", 1);
    scene_sync_format_status(&sync, buf, sizeof(buf));
    ASSERT_TRUE(strstr(buf, "Offline") != NULL);
    ASSERT_TRUE(strstr(buf, "1") != NULL);

    scene_sync_destroy(&sync);
    return 0;
}

static int test_sync_save_force(void) {
    scene_sync_t sync;
    scene_sync_init(&sync, NULL);

    /* save_force should set the force flag */
    scene_sync_save_force(&sync);
    ASSERT_TRUE(sync.force_save_pending);

    /* Consuming the flag clears it */
    bool pending = scene_sync_consume_force_save(&sync);
    ASSERT_TRUE(pending);
    ASSERT_TRUE(!sync.force_save_pending);

    /* Second consume returns false */
    pending = scene_sync_consume_force_save(&sync);
    ASSERT_TRUE(!pending);

    scene_sync_destroy(&sync);
    return 0;
}

static int test_sync_queue_fifo_order(void) {
    scene_sync_t sync;
    scene_sync_config_t cfg = {0};
    cfg.queue_capacity = 8;
    scene_sync_init(&sync, &cfg);

    scene_sync_queue_edit(&sync, "first", 10);
    scene_sync_queue_edit(&sync, "second", 20);
    scene_sync_queue_edit(&sync, "third", 30);

    char buf[256];
    uint32_t id = 0;

    scene_sync_dequeue_edit(&sync, buf, sizeof(buf), &id);
    ASSERT_STR_EQ("first", buf);
    ASSERT_INT_EQ(10, (int)id);

    scene_sync_dequeue_edit(&sync, buf, sizeof(buf), &id);
    ASSERT_STR_EQ("second", buf);
    ASSERT_INT_EQ(20, (int)id);

    scene_sync_dequeue_edit(&sync, buf, sizeof(buf), &id);
    ASSERT_STR_EQ("third", buf);
    ASSERT_INT_EQ(30, (int)id);

    scene_sync_destroy(&sync);
    return 0;
}

static int test_sync_queue_wrap(void) {
    scene_sync_t sync;
    scene_sync_config_t cfg = {0};
    cfg.queue_capacity = 4;
    scene_sync_init(&sync, &cfg);

    /* Fill and drain twice to test wrap-around */
    for (int round = 0; round < 2; round++) {
        for (int i = 0; i < 4; i++) {
            char cmd[32];
            snprintf(cmd, sizeof(cmd), "r%d_c%d", round, i);
            bool ok = scene_sync_queue_edit(&sync, cmd, (uint32_t)(round * 10 + i));
            ASSERT_TRUE(ok);
        }
        for (int i = 0; i < 4; i++) {
            char buf[256];
            uint32_t id;
            bool ok = scene_sync_dequeue_edit(&sync, buf, sizeof(buf), &id);
            ASSERT_TRUE(ok);
            char expected[32];
            snprintf(expected, sizeof(expected), "r%d_c%d", round, i);
            ASSERT_STR_EQ(expected, buf);
        }
    }

    scene_sync_destroy(&sync);
    return 0;
}

/* ---- Test runner ---- */

struct test_case {
    const char *name;
    int (*fn)(void);
};

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static struct test_case TESTS[] = {
    {"sync_init",                test_sync_init},
    {"sync_destroy_safe",        test_sync_destroy_safe},
    {"sync_init_defaults",       test_sync_init_defaults},
    {"sync_queue_edit",          test_sync_queue_edit},
    {"sync_dequeue_edit",        test_sync_dequeue_edit},
    {"sync_queue_overflow",      test_sync_queue_overflow},
    {"sync_in_flight_tracking",  test_sync_in_flight_tracking},
    {"sync_state_transitions",   test_sync_state_transitions},
    {"sync_format_status",       test_sync_format_status},
    {"sync_save_force",          test_sync_save_force},
    {"sync_queue_fifo_order",    test_sync_queue_fifo_order},
    {"sync_queue_wrap",          test_sync_queue_wrap},
};

int main(void) {
    size_t total = ARRAY_SIZE(TESTS);
    size_t passed = 0;

    for (size_t i = 0; i < total; ++i) {
        struct test_case *tc = &TESTS[i];
        printf("RUN  %s\n", tc->name);
        int rc = tc->fn();
        if (rc == 0) {
            printf("  OK %s\n", tc->name);
            passed++;
        } else {
            fprintf(stderr, "FAIL %s\n", tc->name);
            break;
        }
    }

    printf("\n%zu / %zu tests passed\n", passed, total);
    return (passed == total) ? 0 : 1;
}
