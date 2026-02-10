/**
 * @file p007_net_client_tx_tests.c
 * @brief Tests for client TX runtime: outbound queue, pump, rate limiting,
 *        thread lifecycle.
 *
 * Tests use a loopback sendto callback that captures transmitted frames
 * into a ring buffer for assertion.
 */

#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include <time.h>

#include "ferrum/net/client/runtime_tx.h"

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
/*  Captured send sink: records frames sent by TX pump.               */
/* ------------------------------------------------------------------ */

#define SINK_MAX_FRAMES  64
#define SINK_FRAME_SIZE 256

typedef struct send_sink {
    uint8_t frames[SINK_MAX_FRAMES][SINK_FRAME_SIZE];
    size_t  sizes[SINK_MAX_FRAMES];
    uint32_t count;
} send_sink_t;

static send_sink_t g_sink;

static void sink_reset(void) {
    memset(&g_sink, 0, sizeof(g_sink));
}

/**
 * sendto callback: captures the frame into the sink.
 * Returns 0 on success, -1 if sink is full.
 */
static int sink_sendto(void *user, const uint8_t *data, size_t len) {
    send_sink_t *sink = (send_sink_t *)user;
    if (sink->count >= SINK_MAX_FRAMES) { return -1; }
    if (len > SINK_FRAME_SIZE) { len = SINK_FRAME_SIZE; }
    memcpy(sink->frames[sink->count], data, len);
    sink->sizes[sink->count] = len;
    sink->count++;
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Helper: sleep for a few ms                                        */
/* ------------------------------------------------------------------ */

static void sleep_ms(int ms) {
    struct timespec ts = { .tv_sec = 0, .tv_nsec = ms * 1000000 };
    nanosleep(&ts, NULL);
}

/* ------------------------------------------------------------------ */
/*  1. Create / destroy lifecycle without starting thread.            */
/* ------------------------------------------------------------------ */

TEST(test_create_destroy) {
    fr_client_tx_config_t cfg = {0};
    cfg.max_channels = 4;
    cfg.sendto = sink_sendto;
    cfg.sendto_user = &g_sink;

    fr_client_tx_t *tx = fr_client_tx_create(&cfg);
    ASSERT(tx != NULL);
    fr_client_tx_destroy(tx);
}

/* ------------------------------------------------------------------ */
/*  2. Enqueue a message and manually pump once → frame appears.      */
/* ------------------------------------------------------------------ */

TEST(test_enqueue_and_pump) {
    sink_reset();

    fr_client_tx_config_t cfg = {0};
    cfg.max_channels = 4;
    cfg.sendto = sink_sendto;
    cfg.sendto_user = &g_sink;

    fr_client_tx_t *tx = fr_client_tx_create(&cfg);
    ASSERT(tx != NULL);

    /* Enqueue a 10-byte message on channel 0. */
    uint8_t payload[10];
    memset(payload, 0xAB, sizeof(payload));
    bool ok = fr_client_tx_enqueue(tx, 0, payload, sizeof(payload));
    ASSERT(ok);

    /* Pump once: should flush to sink. */
    uint32_t flushed = fr_client_tx_pump_once(tx);
    ASSERT(flushed == 1);
    ASSERT(g_sink.count == 1);

    /* Frame should contain the payload (after the 4-byte frame header). */
    ASSERT(g_sink.sizes[0] == 10 + 4);
    ASSERT(memcmp(g_sink.frames[0] + 4, payload, 10) == 0);

    fr_client_tx_destroy(tx);
}

/* ------------------------------------------------------------------ */
/*  3. Multiple messages batched in a single pump cycle.              */
/* ------------------------------------------------------------------ */

TEST(test_batch_multiple_messages) {
    sink_reset();

    fr_client_tx_config_t cfg = {0};
    cfg.max_channels = 4;
    cfg.sendto = sink_sendto;
    cfg.sendto_user = &g_sink;

    fr_client_tx_t *tx = fr_client_tx_create(&cfg);
    ASSERT(tx != NULL);

    /* Enqueue 5 messages on channel 0. */
    for (int i = 0; i < 5; i++) {
        uint8_t msg[8];
        memset(msg, (uint8_t)i, sizeof(msg));
        ASSERT(fr_client_tx_enqueue(tx, 0, msg, sizeof(msg)));
    }

    /* Single pump should flush all 5. */
    uint32_t flushed = fr_client_tx_pump_once(tx);
    ASSERT(flushed == 5);
    ASSERT(g_sink.count == 5);

    /* Verify each frame payload. */
    for (int i = 0; i < 5; i++) {
        uint8_t expected[8];
        memset(expected, (uint8_t)i, sizeof(expected));
        ASSERT(memcmp(g_sink.frames[i] + 4, expected, 8) == 0);
    }

    fr_client_tx_destroy(tx);
}

/* ------------------------------------------------------------------ */
/*  4. Rate limiting: caps packets per pump cycle.                    */
/* ------------------------------------------------------------------ */

TEST(test_rate_limit_packets_per_pump) {
    sink_reset();

    fr_client_tx_config_t cfg = {0};
    cfg.max_channels = 4;
    cfg.max_packets_per_pump = 3;    /* cap at 3 per pump */
    cfg.sendto = sink_sendto;
    cfg.sendto_user = &g_sink;

    fr_client_tx_t *tx = fr_client_tx_create(&cfg);
    ASSERT(tx != NULL);

    /* Enqueue 7 messages. */
    for (int i = 0; i < 7; i++) {
        uint8_t msg[4] = { (uint8_t)i, 0, 0, 0 };
        ASSERT(fr_client_tx_enqueue(tx, 0, msg, sizeof(msg)));
    }

    /* First pump: should only send 3. */
    uint32_t f1 = fr_client_tx_pump_once(tx);
    ASSERT(f1 == 3);
    ASSERT(g_sink.count == 3);

    /* Second pump: next 3. */
    uint32_t f2 = fr_client_tx_pump_once(tx);
    ASSERT(f2 == 3);
    ASSERT(g_sink.count == 6);

    /* Third pump: remaining 1. */
    uint32_t f3 = fr_client_tx_pump_once(tx);
    ASSERT(f3 == 1);
    ASSERT(g_sink.count == 7);

    fr_client_tx_destroy(tx);
}

/* ------------------------------------------------------------------ */
/*  5. Multiple channels: messages drain round-robin.                 */
/* ------------------------------------------------------------------ */

TEST(test_multi_channel_drain) {
    sink_reset();

    fr_client_tx_config_t cfg = {0};
    cfg.max_channels = 4;
    cfg.sendto = sink_sendto;
    cfg.sendto_user = &g_sink;

    fr_client_tx_t *tx = fr_client_tx_create(&cfg);
    ASSERT(tx != NULL);

    /* Enqueue on channels 0, 1, 2. */
    uint8_t a[4] = { 0xAA, 0, 0, 0 };
    uint8_t b[4] = { 0xBB, 0, 0, 0 };
    uint8_t c[4] = { 0xCC, 0, 0, 0 };
    ASSERT(fr_client_tx_enqueue(tx, 0, a, 4));
    ASSERT(fr_client_tx_enqueue(tx, 1, b, 4));
    ASSERT(fr_client_tx_enqueue(tx, 2, c, 4));

    uint32_t flushed = fr_client_tx_pump_once(tx);
    ASSERT(flushed == 3);
    ASSERT(g_sink.count == 3);

    /* All three channels' messages should have been sent. */
    /* Channel IDs are in bytes 2-3 of the frame header (LE). */
    int seen_ch[3] = {0, 0, 0};
    for (uint32_t i = 0; i < g_sink.count; i++) {
        uint16_t ch = (uint16_t)(g_sink.frames[i][2] |
                                 ((uint16_t)g_sink.frames[i][3] << 8));
        if (ch < 3) { seen_ch[ch] = 1; }
    }
    ASSERT(seen_ch[0] && seen_ch[1] && seen_ch[2]);

    fr_client_tx_destroy(tx);
}

/* ------------------------------------------------------------------ */
/*  6. Thread start/stop lifecycle.                                   */
/* ------------------------------------------------------------------ */

TEST(test_thread_start_stop) {
    sink_reset();

    fr_client_tx_config_t cfg = {0};
    cfg.max_channels = 4;
    cfg.sendto = sink_sendto;
    cfg.sendto_user = &g_sink;

    fr_client_tx_t *tx = fr_client_tx_create(&cfg);
    ASSERT(tx != NULL);

    /* Enqueue a message before starting. */
    uint8_t msg[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    ASSERT(fr_client_tx_enqueue(tx, 0, msg, sizeof(msg)));

    /* Start TX thread. */
    bool started = fr_client_tx_start(tx);
    ASSERT(started);

    /* Give the pump thread time to drain the queue. */
    sleep_ms(50);

    /* Stop the thread cleanly. */
    bool stopped = fr_client_tx_stop(tx);
    ASSERT(stopped);

    /* Message should have been sent. */
    ASSERT(g_sink.count >= 1);

    fr_client_tx_destroy(tx);
}

/* ------------------------------------------------------------------ */
/*  7. Enqueue on invalid channel returns false.                      */
/* ------------------------------------------------------------------ */

TEST(test_enqueue_invalid_channel) {
    fr_client_tx_config_t cfg = {0};
    cfg.max_channels = 2;
    cfg.sendto = sink_sendto;
    cfg.sendto_user = &g_sink;

    fr_client_tx_t *tx = fr_client_tx_create(&cfg);
    ASSERT(tx != NULL);

    uint8_t msg[4] = {0};
    /* Channel 5 exceeds max_channels=2 → should fail. */
    ASSERT(!fr_client_tx_enqueue(tx, 5, msg, sizeof(msg)));
    /* Channel 0 → should succeed. */
    ASSERT(fr_client_tx_enqueue(tx, 0, msg, sizeof(msg)));

    fr_client_tx_destroy(tx);
}

/* ------------------------------------------------------------------ */
/*  8. Empty pump returns zero.                                       */
/* ------------------------------------------------------------------ */

TEST(test_empty_pump) {
    sink_reset();

    fr_client_tx_config_t cfg = {0};
    cfg.max_channels = 4;
    cfg.sendto = sink_sendto;
    cfg.sendto_user = &g_sink;

    fr_client_tx_t *tx = fr_client_tx_create(&cfg);
    ASSERT(tx != NULL);

    /* Pump with nothing enqueued. */
    uint32_t flushed = fr_client_tx_pump_once(tx);
    ASSERT(flushed == 0);
    ASSERT(g_sink.count == 0);

    fr_client_tx_destroy(tx);
}

/* ------------------------------------------------------------------ */
/*  Main                                                              */
/* ------------------------------------------------------------------ */

int main(void) {
    printf("p007_net_client_tx_tests:\n");
    RUN(test_create_destroy);
    RUN(test_enqueue_and_pump);
    RUN(test_batch_multiple_messages);
    RUN(test_rate_limit_packets_per_pump);
    RUN(test_multi_channel_drain);
    RUN(test_thread_start_stop);
    RUN(test_enqueue_invalid_channel);
    RUN(test_empty_pump);
    printf("%d/%d tests passed\n", g_pass, g_pass + g_fail);
    return g_fail > 0 ? 1 : 0;
}
