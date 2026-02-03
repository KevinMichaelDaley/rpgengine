#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <time.h>
#include "ferrum/ferrum.h"

static void ASSERT_TRUE(int cond) { if (!cond) { fprintf(stderr, "ASSERT_TRUE failed\n"); exit(1);} }
static void ASSERT_EQ_INT(int a, int b) { if (a!=b) { fprintf(stderr, "ASSERT_EQ_INT %d != %d\n", a, b); exit(1);} }

/* Handler counters */
typedef struct handler_ctx {
    atomic_int count;
    char expect_prefix;
} handler_ctx_t;

static void handler_fn_a(const uint8_t *data, size_t len, void *user) {
    handler_ctx_t *ctx = (handler_ctx_t*)user;
    ASSERT_TRUE(len >= 2);
    ASSERT_TRUE(((char)data[0]) == ctx->expect_prefix);
    atomic_fetch_add_explicit(&ctx->count, 1, memory_order_relaxed);
}

static void handler_fn_b(const uint8_t *data, size_t len, void *user) {
    handler_ctx_t *ctx = (handler_ctx_t*)user;
    ASSERT_TRUE(len >= 2);
    ASSERT_TRUE(((char)data[0]) == ctx->expect_prefix);
    atomic_fetch_add_explicit(&ctx->count, 1, memory_order_relaxed);
}

int main(void) {
    fprintf(stderr, "RUN topic_dispatch_happy_path\n");
    {
        job_system_t jobs; ASSERT_EQ_INT(0, job_system_create(&jobs, 2, 256, 1<<16, 2048, 0));
        ASSERT_EQ_INT(0, job_system_start(&jobs));
        fr_topic_channel_config_t cfg = { .capacity = 64 };
        fr_topic_channel_t *topic0 = fr_topic_channel_create(&cfg);
        ASSERT_TRUE(topic0 != NULL);
        fr_topic_channel_t *topics[1] = { topic0 };
        /* Dispatcher create/start/register */
        extern struct fr_topic_dispatcher; /* opaque */
        typedef struct fr_topic_dispatcher fr_topic_dispatcher_t; /* forward for tests */
        /* Public API will be in header. Using forward decl here as TDD expects missing symbols initially. */
        fr_topic_dispatcher_t *disp = NULL;
        /* create */
        disp = fr_topic_dispatcher_create(&jobs, topics, 1);
        ASSERT_TRUE(disp != NULL);
        handler_ctx_t ctx = { .count = 0, .expect_prefix = 'A' };
        ASSERT_EQ_INT(0, fr_topic_dispatcher_register(disp, 0, handler_fn_a, &ctx, 0, UINT32_MAX));
        ASSERT_EQ_INT(0, fr_topic_dispatcher_start(disp));
        /* Push messages */
        const char *msgs[] = { "A1", "A2", "A3", "A4" };
        for (int i=0;i<4;i++) {
            ASSERT_TRUE(fr_topic_channel_push(topic0, (const uint8_t*)msgs[i], strlen(msgs[i])));
        }
        /* Wait for processing */
        struct timespec ts = {0}; ts.tv_sec=0; ts.tv_nsec=5*1000*1000; /* 5ms */
        for (int spin=0; spin<200 && atomic_load(&ctx.count) < 4; ++spin) { nanosleep(&ts, NULL); }
        ASSERT_EQ_INT(4, atomic_load(&ctx.count));
        fr_topic_dispatcher_stop(disp);
        fr_topic_dispatcher_destroy(disp);
        fr_topic_channel_destroy(topic0);
        job_system_shutdown(&jobs);
        fprintf(stderr, "OK topic_dispatch_happy_path\n");
    }

    fprintf(stderr, "RUN topic_dispatch_multi_topic\n");
    {
        job_system_t jobs; ASSERT_EQ_INT(0, job_system_create(&jobs, 2, 256, 1<<16, 2048, 0));
        ASSERT_EQ_INT(0, job_system_start(&jobs));
        fr_topic_channel_config_t cfg = { .capacity = 64 };
        fr_topic_channel_t *topic0 = fr_topic_channel_create(&cfg);
        fr_topic_channel_t *topic1 = fr_topic_channel_create(&cfg);
        ASSERT_TRUE(topic0 && topic1);
        fr_topic_channel_t *topics[2] = { topic0, topic1 };
        fr_topic_dispatcher_t *disp = fr_topic_dispatcher_create(&jobs, topics, 2);
        ASSERT_TRUE(disp != NULL);
        handler_ctx_t ctxA = { .count = 0, .expect_prefix = 'A' };
        handler_ctx_t ctxB = { .count = 0, .expect_prefix = 'B' };
        ASSERT_EQ_INT(0, fr_topic_dispatcher_register(disp, 0, handler_fn_a, &ctxA, 1, 0));
        ASSERT_EQ_INT(0, fr_topic_dispatcher_register(disp, 1, handler_fn_b, &ctxB, -1, UINT32_MAX));
        ASSERT_EQ_INT(0, fr_topic_dispatcher_start(disp));
        const char *aMsgs[] = { "A1", "A2", "A3" };
        const char *bMsgs[] = { "B1", "B2" };
        for (int i=0;i<3;i++) { ASSERT_TRUE(fr_topic_channel_push(topic0, (const uint8_t*)aMsgs[i], strlen(aMsgs[i]))); }
        for (int i=0;i<2;i++) { ASSERT_TRUE(fr_topic_channel_push(topic1, (const uint8_t*)bMsgs[i], strlen(bMsgs[i]))); }
        struct timespec ts = {0}; ts.tv_sec=0; ts.tv_nsec=5*1000*1000;
        for (int spin=0; spin<200 && (atomic_load(&ctxA.count) < 3 || atomic_load(&ctxB.count) < 2); ++spin) { nanosleep(&ts, NULL); }
        ASSERT_EQ_INT(3, atomic_load(&ctxA.count));
        ASSERT_EQ_INT(2, atomic_load(&ctxB.count));
        fr_topic_dispatcher_stop(disp);
        fr_topic_dispatcher_destroy(disp);
        fr_topic_channel_destroy(topic0);
        fr_topic_channel_destroy(topic1);
        job_system_shutdown(&jobs);
        fprintf(stderr, "OK topic_dispatch_multi_topic\n");
    }

    fprintf(stderr, "RUN topic_dispatch_unregistered_drops\n");
    {
        job_system_t jobs; ASSERT_EQ_INT(0, job_system_create(&jobs, 2, 256, 1<<16, 2048, 0));
        ASSERT_EQ_INT(0, job_system_start(&jobs));
        fr_topic_channel_config_t cfg = { .capacity = 64 };
        fr_topic_channel_t *topic0 = fr_topic_channel_create(&cfg);
        ASSERT_TRUE(topic0 != NULL);
        fr_topic_channel_t *topics[1] = { topic0 };
        fr_topic_dispatcher_t *disp = fr_topic_dispatcher_create(&jobs, topics, 1);
        ASSERT_TRUE(disp != NULL);
        ASSERT_EQ_INT(0, fr_topic_dispatcher_start(disp));
        const char *msgs[] = { "Z1", "Z2" };
        for (int i=0;i<2;i++) { ASSERT_TRUE(fr_topic_channel_push(topic0, (const uint8_t*)msgs[i], strlen(msgs[i]))); }
        struct timespec ts = {0}; ts.tv_sec=0; ts.tv_nsec=5*1000*1000;
        nanosleep(&ts, NULL);
        /* No handler, but ensure dispatcher stayed stable */
        fr_topic_dispatcher_stop(disp);
        fr_topic_dispatcher_destroy(disp);
        fr_topic_channel_destroy(topic0);
        job_system_shutdown(&jobs);
        fprintf(stderr, "OK topic_dispatch_unregistered_drops\n");
    }

    return 0;
}
