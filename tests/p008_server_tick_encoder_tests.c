/**
 * @file p008_server_tick_encoder_tests.c
 * @brief Tests for the server tick encoder: dispatches event + state
 *        encoding jobs that feed outbound topic channels.
 */

#include <stdio.h>
#include <string.h>

#include "ferrum/server/tick_encoder.h"
#include "ferrum/net/topic_channel.h"

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
/*  Mock encoder callbacks: write marker bytes to outbound topics.    */
/* ------------------------------------------------------------------ */

static int mock_event_encode(void *user, uint16_t client_id,
                             fr_topic_channel_t *out_reliable,
                             uint64_t tick) {
    (void)user;
    /* Write a marker: [0xEE][client_id_lo][tick_lo] */
    uint8_t marker[3] = { 0xEE, (uint8_t)client_id, (uint8_t)tick };
    fr_topic_channel_push(out_reliable, marker, sizeof(marker));
    return 0;
}

static int mock_state_encode(void *user, uint16_t client_id,
                             fr_topic_channel_t *out_unreliable,
                             uint64_t tick) {
    (void)user;
    /* Write a marker: [0xBB][client_id_lo][tick_lo] */
    uint8_t marker[3] = { 0xBB, (uint8_t)client_id, (uint8_t)tick };
    fr_topic_channel_push(out_unreliable, marker, sizeof(marker));
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Helper: create topic channels for a few clients.                  */
/* ------------------------------------------------------------------ */

#define MAX_TEST_CLIENTS 4

typedef struct test_topics {
    fr_topic_channel_t *reliable[MAX_TEST_CLIENTS];
    fr_topic_channel_t *unreliable[MAX_TEST_CLIENTS];
    uint8_t active[MAX_TEST_CLIENTS];
    uint16_t count;
} test_topics_t;

static test_topics_t g_topics;

static void topics_create(uint16_t n) {
    memset(&g_topics, 0, sizeof(g_topics));
    g_topics.count = (n > MAX_TEST_CLIENTS) ? MAX_TEST_CLIENTS : n;
    fr_topic_channel_config_t tcfg = {0};
    tcfg.capacity = 32;
    tcfg.capacity_bytes = 4096;
    tcfg.max_message_size = 256;
    for (uint16_t i = 0; i < g_topics.count; i++) {
        g_topics.reliable[i] = fr_topic_channel_create(&tcfg);
        g_topics.unreliable[i] = fr_topic_channel_create(&tcfg);
        g_topics.active[i] = 1;
    }
}

static void topics_destroy(void) {
    for (uint16_t i = 0; i < g_topics.count; i++) {
        if (g_topics.reliable[i]) {
            fr_topic_channel_destroy(g_topics.reliable[i]);
        }
        if (g_topics.unreliable[i]) {
            fr_topic_channel_destroy(g_topics.unreliable[i]);
        }
    }
    memset(&g_topics, 0, sizeof(g_topics));
}

static bool test_get_topics_cb(void *user, uint16_t client_id,
                               fr_topic_channel_t **out_reliable,
                               fr_topic_channel_t **out_unreliable) {
    (void)user;
    if (client_id >= g_topics.count || !g_topics.active[client_id]) {
        return false;
    }
    *out_reliable = g_topics.reliable[client_id];
    *out_unreliable = g_topics.unreliable[client_id];
    return true;
}

/* ------------------------------------------------------------------ */
/*  1. Init + single tick encodes events + state for each client.     */
/* ------------------------------------------------------------------ */

TEST(test_encode_single_tick) {
    topics_create(2);

    fr_server_tick_encoder_config_t cfg = {0};
    cfg.max_clients = 2;
    cfg.get_client_out_topics = test_get_topics_cb;
    cfg.io_user = NULL;
    cfg.encode_events = mock_event_encode;
    cfg.encode_state = mock_state_encode;
    cfg.encode_user = NULL;

    fr_server_tick_encoder_t enc;
    int rc = fr_server_tick_encoder_init(&enc, &cfg);
    ASSERT(rc == 0);

    /* Run encoder for tick 5. */
    rc = fr_server_tick_encoder_run(&enc, 5);
    ASSERT(rc == 0);

    /* Each client should have one event + one state message. */
    for (uint16_t ci = 0; ci < 2; ci++) {
        uint8_t buf[16];
        size_t len;

        /* Reliable: event marker. */
        len = sizeof(buf);
        bool ok = fr_topic_channel_pop(g_topics.reliable[ci], buf, &len);
        ASSERT(ok);
        ASSERT(len == 3);
        ASSERT(buf[0] == 0xEE);
        ASSERT(buf[1] == (uint8_t)ci);
        ASSERT(buf[2] == 5);

        /* Unreliable: state marker. */
        len = sizeof(buf);
        ok = fr_topic_channel_pop(g_topics.unreliable[ci], buf, &len);
        ASSERT(ok);
        ASSERT(len == 3);
        ASSERT(buf[0] == 0xBB);
        ASSERT(buf[1] == (uint8_t)ci);
        ASSERT(buf[2] == 5);
    }

    topics_destroy();
}

/* ------------------------------------------------------------------ */
/*  2. Inactive client is skipped.                                    */
/* ------------------------------------------------------------------ */

TEST(test_skip_inactive_client) {
    topics_create(3);
    g_topics.active[1] = 0;  /* client 1 inactive */

    fr_server_tick_encoder_config_t cfg = {0};
    cfg.max_clients = 3;
    cfg.get_client_out_topics = test_get_topics_cb;
    cfg.encode_events = mock_event_encode;
    cfg.encode_state = mock_state_encode;

    fr_server_tick_encoder_t enc;
    fr_server_tick_encoder_init(&enc, &cfg);
    fr_server_tick_encoder_run(&enc, 1);

    /* Client 0 and 2 got messages, client 1 did not. */
    uint8_t buf[16];
    size_t len;

    len = sizeof(buf);
    ASSERT(fr_topic_channel_pop(g_topics.reliable[0], buf, &len));
    ASSERT(buf[1] == 0);

    /* Client 1's reliable topic should be empty. */
    len = sizeof(buf);
    ASSERT(!fr_topic_channel_pop(g_topics.reliable[1], buf, &len));

    len = sizeof(buf);
    ASSERT(fr_topic_channel_pop(g_topics.reliable[2], buf, &len));
    ASSERT(buf[1] == 2);

    topics_destroy();
}

/* ------------------------------------------------------------------ */
/*  3. Events-only encoder (no state callback).                       */
/* ------------------------------------------------------------------ */

TEST(test_events_only) {
    topics_create(1);

    fr_server_tick_encoder_config_t cfg = {0};
    cfg.max_clients = 1;
    cfg.get_client_out_topics = test_get_topics_cb;
    cfg.encode_events = mock_event_encode;
    cfg.encode_state = NULL;  /* no state encoder */

    fr_server_tick_encoder_t enc;
    fr_server_tick_encoder_init(&enc, &cfg);
    fr_server_tick_encoder_run(&enc, 10);

    /* Reliable should have event message. */
    uint8_t buf[16];
    size_t len = sizeof(buf);
    ASSERT(fr_topic_channel_pop(g_topics.reliable[0], buf, &len));
    ASSERT(buf[0] == 0xEE);

    /* Unreliable should be empty. */
    len = sizeof(buf);
    ASSERT(!fr_topic_channel_pop(g_topics.unreliable[0], buf, &len));

    topics_destroy();
}

/* ------------------------------------------------------------------ */
/*  4. State-only encoder (no event callback).                        */
/* ------------------------------------------------------------------ */

TEST(test_state_only) {
    topics_create(1);

    fr_server_tick_encoder_config_t cfg = {0};
    cfg.max_clients = 1;
    cfg.get_client_out_topics = test_get_topics_cb;
    cfg.encode_events = NULL;
    cfg.encode_state = mock_state_encode;

    fr_server_tick_encoder_t enc;
    fr_server_tick_encoder_init(&enc, &cfg);
    fr_server_tick_encoder_run(&enc, 7);

    /* Reliable should be empty. */
    uint8_t buf[16];
    size_t len = sizeof(buf);
    ASSERT(!fr_topic_channel_pop(g_topics.reliable[0], buf, &len));

    /* Unreliable should have state message. */
    len = sizeof(buf);
    ASSERT(fr_topic_channel_pop(g_topics.unreliable[0], buf, &len));
    ASSERT(buf[0] == 0xBB);

    topics_destroy();
}

/* ------------------------------------------------------------------ */
/*  5. Multiple ticks: encoder runs per tick with correct tick ID.    */
/* ------------------------------------------------------------------ */

TEST(test_multiple_ticks) {
    topics_create(1);

    fr_server_tick_encoder_config_t cfg = {0};
    cfg.max_clients = 1;
    cfg.get_client_out_topics = test_get_topics_cb;
    cfg.encode_events = mock_event_encode;
    cfg.encode_state = mock_state_encode;

    fr_server_tick_encoder_t enc;
    fr_server_tick_encoder_init(&enc, &cfg);

    for (uint64_t t = 0; t < 5; t++) {
        fr_server_tick_encoder_run(&enc, t);
    }

    /* Should have 5 events + 5 states queued. Pop and verify tick IDs. */
    for (uint64_t t = 0; t < 5; t++) {
        uint8_t buf[16];
        size_t len = sizeof(buf);
        ASSERT(fr_topic_channel_pop(g_topics.reliable[0], buf, &len));
        ASSERT(buf[0] == 0xEE);
        ASSERT(buf[2] == (uint8_t)t);
    }
    for (uint64_t t = 0; t < 5; t++) {
        uint8_t buf[16];
        size_t len = sizeof(buf);
        ASSERT(fr_topic_channel_pop(g_topics.unreliable[0], buf, &len));
        ASSERT(buf[0] == 0xBB);
        ASSERT(buf[2] == (uint8_t)t);
    }

    topics_destroy();
}

/* ------------------------------------------------------------------ */
/*  6. Invalid init returns error.                                    */
/* ------------------------------------------------------------------ */

TEST(test_invalid_init) {
    fr_server_tick_encoder_t enc;

    /* NULL config. */
    ASSERT(fr_server_tick_encoder_init(&enc, NULL) != 0);

    /* Zero max_clients. */
    fr_server_tick_encoder_config_t cfg = {0};
    cfg.max_clients = 0;
    ASSERT(fr_server_tick_encoder_init(&enc, &cfg) != 0);

    /* No topic getter. */
    cfg.max_clients = 1;
    cfg.get_client_out_topics = NULL;
    ASSERT(fr_server_tick_encoder_init(&enc, &cfg) != 0);
}

/* ------------------------------------------------------------------ */
/*  7. Encoder with zero clients does nothing.                        */
/* ------------------------------------------------------------------ */

TEST(test_zero_active_clients) {
    topics_create(2);
    /* Mark all inactive. */
    g_topics.active[0] = 0;
    g_topics.active[1] = 0;

    fr_server_tick_encoder_config_t cfg = {0};
    cfg.max_clients = 2;
    cfg.get_client_out_topics = test_get_topics_cb;
    cfg.encode_events = mock_event_encode;
    cfg.encode_state = mock_state_encode;

    fr_server_tick_encoder_t enc;
    fr_server_tick_encoder_init(&enc, &cfg);
    int rc = fr_server_tick_encoder_run(&enc, 1);
    ASSERT(rc == 0);

    /* No messages produced. */
    uint8_t buf[16];
    size_t len = sizeof(buf);
    ASSERT(!fr_topic_channel_pop(g_topics.reliable[0], buf, &len));
    ASSERT(!fr_topic_channel_pop(g_topics.unreliable[0], buf, &len));

    topics_destroy();
}

/* ------------------------------------------------------------------ */
/*  8. Null encoder pointer does not crash.                           */
/* ------------------------------------------------------------------ */

TEST(test_null_safety) {
    ASSERT(fr_server_tick_encoder_run(NULL, 0) != 0);
    fr_server_tick_encoder_t enc;
    ASSERT(fr_server_tick_encoder_init(NULL, NULL) != 0);
    ASSERT(fr_server_tick_encoder_init(&enc, NULL) != 0);
}

/* ------------------------------------------------------------------ */
/*  Main                                                              */
/* ------------------------------------------------------------------ */

int main(void) {
    printf("p008_server_tick_encoder_tests:\n");
    RUN(test_encode_single_tick);
    RUN(test_skip_inactive_client);
    RUN(test_events_only);
    RUN(test_state_only);
    RUN(test_multiple_ticks);
    RUN(test_invalid_init);
    RUN(test_zero_active_clients);
    RUN(test_null_safety);
    printf("%d/%d tests passed\n", g_pass, g_pass + g_fail);
    return g_fail > 0 ? 1 : 0;
}
