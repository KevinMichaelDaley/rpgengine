/**
 * @file tick_encoder.c
 * @brief Server tick encoder: init + run.
 *
 * Non-static functions: 2 (init, run).
 */

#include "ferrum/server/tick_encoder.h"
#include <string.h>

int fr_server_tick_encoder_init(fr_server_tick_encoder_t *enc,
                                const fr_server_tick_encoder_config_t *cfg) {
    if (!enc || !cfg) { return -1; }
    if (cfg->max_clients == 0 || !cfg->get_client_out_topics) { return -1; }

    memset(enc, 0, sizeof(*enc));
    enc->max_clients = cfg->max_clients;
    enc->get_topics = cfg->get_client_out_topics;
    enc->io_user = cfg->io_user;
    enc->encode_events = cfg->encode_events;
    enc->encode_state = cfg->encode_state;
    enc->encode_user = cfg->encode_user;
    return 0;
}

int fr_server_tick_encoder_run(fr_server_tick_encoder_t *enc,
                               uint64_t tick) {
    if (!enc) { return -1; }

    for (uint16_t ci = 0; ci < enc->max_clients; ci++) {
        fr_topic_channel_t *reliable = NULL;
        fr_topic_channel_t *unreliable = NULL;

        /* Ask the runtime if this client is active and get its topics. */
        if (!enc->get_topics(enc->io_user, ci, &reliable, &unreliable)) {
            continue; /* inactive or disconnected */
        }

        /* Encode events → reliable topic. */
        if (enc->encode_events && reliable) {
            enc->encode_events(enc->encode_user, ci, reliable, tick);
        }

        /* Encode state → unreliable topic. */
        if (enc->encode_state && unreliable) {
            enc->encode_state(enc->encode_user, ci, unreliable, tick);
        }
    }

    return 0;
}
