#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <time.h>
#include "ferrum/ferrum.h"

static inline uint64_t now_ns(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

typedef struct bench_ctx {
    atomic_int processed;
} bench_ctx_t;

static void bench_handler(const uint8_t *data, size_t len, void *user) {
    (void)data; (void)len; bench_ctx_t *ctx=(bench_ctx_t*)user;
    atomic_fetch_add_explicit(&ctx->processed, 1, memory_order_relaxed);
}

int main(int argc, char **argv) {
    int workers = (argc > 1) ? atoi(argv[1]) : 4;
    int messages = (argc > 2) ? atoi(argv[2]) : 100000;
    int payload_len = (argc > 3) ? atoi(argv[3]) : 32;
    fprintf(stderr, "RUN topic_dispatch_benchmark workers=%d messages=%d payload=%d\n", workers, messages, payload_len);

    job_system_t jobs; if (job_system_create(&jobs, (uint32_t)workers, 4096u, 1u<<16, 2048, 0) != 0) return 1;
    if (job_system_start(&jobs) != 0) return 1;

    fr_topic_channel_config_t cfg = { .capacity = 1u<<12 };
    fr_topic_channel_t *topic0 = fr_topic_channel_create(&cfg);
    fr_topic_channel_t *topics[1] = { topic0 };
    fr_topic_dispatcher_t *disp = fr_topic_dispatcher_create(&jobs, topics, 1);
    bench_ctx_t ctx; atomic_store(&ctx.processed, 0);
    fr_topic_dispatcher_register(disp, 0, bench_handler, &ctx, 0, UINT32_MAX);
    fr_topic_dispatcher_start(disp);

    uint8_t *payload = (uint8_t*)malloc((size_t)payload_len);
    memset(payload, 0xAB, (size_t)payload_len);
    uint64_t t0 = now_ns();
    for (int i=0;i<messages;i++) {
        while (!fr_topic_channel_push(topic0, payload, (size_t)payload_len)) {
            /* channel full, yield briefly */ struct timespec ts={0, 1000*1000}; nanosleep(&ts, NULL);
        }
    }
    free(payload);

    while (atomic_load(&ctx.processed) < messages) {
        struct timespec ts={0, 1000*1000}; nanosleep(&ts, NULL);
    }
    uint64_t t1 = now_ns();
    double secs = (double)(t1 - t0) / 1e9;
    double mps = (double)messages / secs;
    fprintf(stderr, "OK topic_dispatch_benchmark: %.2f msgs/s over %.2fs (workers=%d)\n", mps, secs, workers);

    fr_topic_dispatcher_stop(disp);
    fr_topic_dispatcher_destroy(disp);
    fr_topic_channel_destroy(topic0);
    job_system_shutdown(&jobs);
    return 0;
}
