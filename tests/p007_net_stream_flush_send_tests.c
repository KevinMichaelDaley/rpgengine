#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/net/stream.h"

struct send_ctx {
    int call_count;
    int fail_on_call; /* 0 = never fail */

    int saved_count;
    uint8_t saved1[64];
    size_t saved1_len;
    uint8_t saved2[64];
    size_t saved2_len;
};

static int sendto_cb(void *user, const uint8_t *data, size_t len) {
    struct send_ctx *ctx = (struct send_ctx *)user;
    assert(ctx != NULL);
    assert(data != NULL);

    ctx->call_count++;
    if (ctx->fail_on_call != 0 && ctx->call_count == ctx->fail_on_call) {
        return -1;
    }

    if (ctx->saved_count == 0) {
        assert(len <= sizeof(ctx->saved1));
        memcpy(ctx->saved1, data, len);
        ctx->saved1_len = len;
    } else if (ctx->saved_count == 1) {
        assert(len <= sizeof(ctx->saved2));
        memcpy(ctx->saved2, data, len);
        ctx->saved2_len = len;
    }

    ctx->saved_count++;
    return 0;
}

static void expect_frame(const uint8_t *frame, size_t len, uint16_t seq, const char *payload) {
    size_t payload_len = strlen(payload);
    assert(frame != NULL);
    assert(len == 4u + payload_len);

    assert(frame[0] == (uint8_t)(seq & 0xFFu));
    assert(frame[1] == (uint8_t)((seq >> 8u) & 0xFFu));
    assert(frame[2] == 0u);
    assert(frame[3] == 0u);
    assert(memcmp(frame + 4u, payload, payload_len) == 0);
}

static int test_flush_does_not_drop_on_send_failure(void) {
    fr_rudp_stream_config_t cfg = {0};
    cfg.reliable_channels = 1u;
    cfg.reliable_slot_count = 4u;
    cfg.max_payload_size = 16u;

    fr_rudp_stream_t *s = fr_rudp_stream_create(&cfg);
    assert(s != NULL);

    assert(fr_rudp_stream_send(s, 0u, (const uint8_t *)"one", 3u));
    assert(fr_rudp_stream_send(s, 0u, (const uint8_t *)"two", 3u));

    struct send_ctx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.fail_on_call = 2; /* fail sending the 2nd frame */

    /* First flush should send only the first frame and stop on the send error.
       The second message must remain queued. */
    uint32_t flushed = fr_rudp_stream_flush_send(s, sendto_cb, &ctx);
    assert(flushed == 1u);
    assert(ctx.saved_count == 1);
    expect_frame(ctx.saved1, ctx.saved1_len, 1u, "one");

    /* Now allow sending; flush should deliver the previously-failed second message. */
    ctx.fail_on_call = 0;
    flushed = fr_rudp_stream_flush_send(s, sendto_cb, &ctx);
    assert(flushed == 1u);
    assert(ctx.saved_count == 2);
    expect_frame(ctx.saved2, ctx.saved2_len, 2u, "two");

    flushed = fr_rudp_stream_flush_send(s, sendto_cb, &ctx);
    assert(flushed == 0u);

    fr_rudp_stream_destroy(s);
    return 0;
}

int main(void) {
    int rc = 0;
    rc |= test_flush_does_not_drop_on_send_failure();
    printf("p007_net_stream_flush_send_tests: OK\n");
    return rc;
}
