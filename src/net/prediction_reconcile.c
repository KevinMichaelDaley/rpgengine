/**
 * @file prediction_reconcile.c
 * @brief Prediction context init and reconciliation.
 *
 * Non-static functions: 2 (init, reconcile).
 */

#include "ferrum/net/prediction.h"
#include <math.h>
#include <string.h>

void net_predict_init(net_predict_ctx_t *ctx,
                      net_predict_input_t *ring_buf,
                      uint32_t ring_cap,
                      const net_predict_config_t *config,
                      net_predict_sim_fn sim_step,
                      void *sim_user) {
    if (!ctx) { return; }
    memset(ctx, 0, sizeof(*ctx));
    net_predict_input_ring_init(&ctx->input_ring, ring_buf, ring_cap);
    if (config) { ctx->config = *config; }
    ctx->sim_step = sim_step;
    ctx->sim_user = sim_user;
}

/** Compute squared distance between two state positions. */
static float state_error_sq(const net_predict_state_t *a,
                            const net_predict_state_t *b) {
    float dx = a->position[0] - b->position[0];
    float dy = a->position[1] - b->position[1];
    float dz = a->position[2] - b->position[2];
    return dx * dx + dy * dy + dz * dz;
}

/** Lerp a single float. */
static float lerpf(float a, float b, float t) {
    return a + (b - a) * t;
}

int net_predict_reconcile(net_predict_ctx_t *ctx,
                          const net_predict_state_t *server_state,
                          uint32_t confirmed_tick) {
    if (!ctx || !server_state) { return NET_PREDICT_ERR_INVALID; }

    /* Save old predicted state for blending. */
    net_predict_state_t old_predicted = ctx->predicted;

    /* Step 1: rewind to server state. */
    net_predict_state_t replayed = *server_state;

    /* Step 2: replay unconfirmed inputs from (confirmed_tick+1)
     * through (predicted_tick - 1). */
    if (ctx->sim_step) {
        for (uint32_t t = confirmed_tick + 1; t < ctx->predicted_tick; t++) {
            const net_predict_input_t *in =
                net_predict_input_ring_get(&ctx->input_ring, t);
            if (in) {
                ctx->sim_step(&replayed, in, ctx->sim_user);
            }
        }
    }

    /* Step 3: compute error between old prediction and replay. */
    float err_sq = state_error_sq(&old_predicted, &replayed);
    float snap_sq = ctx->config.snap_threshold * ctx->config.snap_threshold;
    float blend_sq = ctx->config.blend_threshold * ctx->config.blend_threshold;

    if (err_sq >= snap_sq) {
        /* Large error → hard snap to replayed. */
        ctx->predicted = replayed;
    } else if (err_sq > blend_sq) {
        /* Small error → blend old predicted toward replayed. */
        float t = ctx->config.blend_rate;
        for (int i = 0; i < 3; i++) {
            ctx->predicted.position[i] =
                lerpf(old_predicted.position[i], replayed.position[i], t);
            ctx->predicted.velocity[i] =
                lerpf(old_predicted.velocity[i], replayed.velocity[i], t);
        }
    }
    /* else: error below blend threshold → keep old predicted. */

    ctx->confirmed_tick = confirmed_tick;
    return NET_PREDICT_OK;
}
