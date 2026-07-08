/**
 * @file npc_demo_tick.c
 * @brief Per-tick NPC scheduler: iterates active NPCs, checks async tasks, logs.
 *
 * Non-static functions (2 of 4 max):
 *   1. npc_demo_tick
 *   2. npc_demo_tick_run_one
 *
 * Static helpers:
 *   - tick_drain_async_
 *   - tick_log_sense_
 */

#include "ferrum/npc/npc_demo.h"
#include "ferrum/aegis/aegis_async.h"
#include "ferrum/aegis/aegis_async_execute.h"
#include "ferrum/aegis/aegis_runtime.h"
#include "ferrum/aegis/aegis_event.h"
#include "ferrum/physics/world.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void tick_drain_async_(struct aegis_async_buffer *buf,
                              const struct phys_world *world) {
    if (!buf || !world) return;
    aegis_async_execute_drain(buf, world, 16);
}

static void tick_log_sense_(const npc_demo_npc_t *npc, uint32_t detected) {
    (void)npc;
    /* printf("SENSE: detected %u entities (tick %u)\n",
           (unsigned)detected, (unsigned)npc->tick_count); */
}

uint32_t npc_demo_tick_run_one(npc_demo_npc_t *npc,
                                struct aegis_script_runtime *script_rt,
                                struct aegis_async_buffer *async_buf,
                                const struct phys_world *world) {
    if (!npc || !npc->active) return 0;

    npc->tick_count++;

    tick_drain_async_(async_buf, world);

    if (script_rt) {
        aegis_event_t ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = aegis_topic_hash("npc_tick");
        ev.source = npc->config.entity_id;
        aegis_script_runtime_publish(script_rt, &ev);
    }

    tick_log_sense_(npc, 0);

    return npc->tick_count;
}

void npc_demo_tick(struct aegis_script_runtime *script_rt,
                   struct aegis_async_buffer *async_buf,
                   const struct phys_world *world) {
    uint32_t count = 0;
    const npc_demo_npc_t *npcs = npc_demo_npc_list(&count);

    for (uint32_t i = 0; i < count; i++) {
        npc_demo_npc_t *npc = (npc_demo_npc_t *)&npcs[i];
        if (!npc->active) continue;
        npc_demo_tick_run_one(npc, script_rt, async_buf, world);
    }
}
