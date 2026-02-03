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
    atomic_ulong processed;
} bench_ctx_t;

static void bench_handler(const uint8_t *data, size_t len, void *user) {
    (void)data; (void)len; bench_ctx_t *ctx=(bench_ctx_t*)user;
    atomic_fetch_add_explicit(&ctx->processed, 1ul, memory_order_relaxed);
}

int main(int argc, char **argv) {
    int workers = (argc > 1) ? atoi(argv[1]) : 4;
    int messages = (argc > 2) ? atoi(argv[2]) : 100000;
    int payload_len = (argc > 3) ? atoi(argv[3]) : 32;
    int channels = (argc > 4) ? atoi(argv[4]) : 1;
    fprintf(stderr, "RUN stream_perf_benchmark workers=%d messages=%d payload=%d channels=%d\n", workers, messages, payload_len, channels);

    job_system_t jobs; if (job_system_create(&jobs, (uint32_t)workers, 4096u, 1u<<16, 2048, 0) != 0) return 1;
    if (job_system_start(&jobs) != 0) return 1;

    fr_topic_channel_config_t tcfg = { .capacity = 1u<<12 };
    fr_topic_channel_t *topic0 = fr_topic_channel_create(&tcfg);
    fr_topic_channel_t *topics[1] = { topic0 };
    fr_topic_dispatcher_t *disp = fr_topic_dispatcher_create(&jobs, topics, 1);
    bench_ctx_t ctx; atomic_store(&ctx.processed, 0);
    fr_topic_dispatcher_register(disp, 0, bench_handler, &ctx, 0, UINT32_MAX);
    fr_topic_dispatcher_start(disp);

    fr_rudp_stream_config_t scfg = {0};
    scfg.reliable_channels = (unsigned)channels;
    scfg.reliable_slot_count = 1u<<12;
    scfg.max_payload_size = (unsigned)payload_len;
    scfg.topics = topics;
    scfg.num_topics = 1;
    fr_rudp_stream_t *stream = fr_rudp_stream_create(&scfg);
    if (!stream) return 1;

    /* Craft frames: [seq:u16 LE][chan:u16 LE][payload] */
    uint8_t *payload = (uint8_t*)malloc((size_t)payload_len);
    memset(payload, 0xCD, (size_t)payload_len);

    uint64_t t0 = now_ns();
    for (int i=0;i<messages;i++) {
        uint16_t seq = (uint16_t)(i+1);
        uint16_t chan = (uint16_t)(i % channels);
        size_t len = 4u + (size_t)payload_len;
        uint8_t *frame = (uint8_t*)malloc(len);
        frame[0] = (uint8_t)(seq & 0xFFu);
        frame[1] = (uint8_t)((seq >> 8) & 0xFFu);
        frame[2] = (uint8_t)(chan & 0xFFu);
        frame[3] = (uint8_t)((chan >> 8) & 0xFFu);
        memcpy(frame+4, payload, (size_t)payload_len);
        while (!fr_rudp_stream_push_frame(stream, frame, len)) {
            struct timespec ts={0, 1000*1000}; nanosleep(&ts, NULL);
        }
        free(frame);
    }
    free(payload);

    while (atomic_load(&ctx.processed) < (unsigned long)messages) {
        struct timespec ts={0, 1000*1000}; nanosleep(&ts, NULL);
    }
    uint64_t t1 = now_ns();
    double secs = (double)(t1 - t0) / 1e9;
    double mps = (double)messages / secs;
    fprintf(stderr, "OK stream_perf_benchmark: %.2f msgs/s over %.2fs (workers=%d)\n", mps, secs, workers);

    fr_rudp_stream_destroy(stream);
    fr_topic_dispatcher_stop(disp);
    fr_topic_dispatcher_destroy(disp);
    fr_topic_channel_destroy(topic0);
    job_system_shutdown(&jobs);
    return 0;
}
