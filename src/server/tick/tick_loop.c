/**
 * @file tick_loop.c
 * @brief Server tick loop: init, step, tick_id.
 *
 * Non-static functions: 3 (init, step, tick_id).
 */

#include "ferrum/server/tick_loop.h"
#include <string.h>

int fr_server_tick_loop_init(fr_server_tick_loop_t *loop,
                             const fr_server_tick_loop_config_t *cfg) {
    if (!loop || !cfg || cfg->tick_hz == 0) { return -1; }

    memset(loop, 0, sizeof(*loop));
    loop->tick_period_us = 1000000 / (uint64_t)cfg->tick_hz;
    loop->max_catchup = (cfg->max_catchup_ticks > 0) ? cfg->max_catchup_ticks : 1;
    loop->on_drain = cfg->on_drain;
    loop->on_physics = cfg->on_physics;
    loop->on_encode = cfg->on_encode;
    loop->on_flush = cfg->on_flush;
    loop->user = cfg->user;
    return 0;
}

int fr_server_tick_loop_step(fr_server_tick_loop_t *loop,
                             uint64_t elapsed_us) {
    if (!loop) { return 0; }

    loop->accumulator_us += elapsed_us;
    int ticks_run = 0;

    while (loop->accumulator_us >= loop->tick_period_us &&
           (uint32_t)ticks_run < loop->max_catchup) {
        /* Drain inbound messages. */
        if (loop->on_drain) { loop->on_drain(loop->user); }

        /* Kick physics. */
        if (loop->on_physics) { loop->on_physics(loop->user); }

        /* Encode replication data. */
        if (loop->on_encode) { loop->on_encode(loop->user); }

        /* Flush outbound. */
        if (loop->on_flush) { loop->on_flush(loop->user); }

        loop->accumulator_us -= loop->tick_period_us;
        loop->tick_id++;
        ticks_run++;
    }

    /* If we hit the catch-up cap, discard remaining accumulator
     * to prevent spiral of death. */
    if ((uint32_t)ticks_run >= loop->max_catchup &&
        loop->accumulator_us >= loop->tick_period_us) {
        loop->accumulator_us = 0;
    }

    return ticks_run;
}

uint64_t fr_server_tick_loop_tick_id(const fr_server_tick_loop_t *loop) {
    if (!loop) { return 0; }
    return loop->tick_id;
}
