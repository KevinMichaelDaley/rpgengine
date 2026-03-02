/**
 * @file aegis_runtime_idle.c
 * @brief Exit-driven idle tracking and unscheduling.
 *
 * When a script exits, the runtime can mark it as "pending unschedule"
 * rather than immediately destroying it. Each tick, the idle counter
 * increments. If an event arrives, the counter resets. If the grace
 * window expires, the script is fully unscheduled.
 */

#include "ferrum/aegis/aegis_runtime.h"

void aegis_runtime_mark_pending_unschedule(aegis_script_instance_t *inst) {
    inst->pending_unschedule = true;
    inst->idle_ticks = 0;
}

void aegis_runtime_tick_idle(aegis_script_runtime_t *rt) {
    for (uint32_t i = 0; i < rt->instance_cap; i++) {
        aegis_script_instance_t *inst = &rt->instances[i];
        if (!inst->active || !inst->pending_unschedule) {
            continue;
        }

        inst->idle_ticks++;

        /* Exceeded grace window → unschedule. */
        if (inst->idle_ticks > rt->config.idle_grace_ticks) {
            aegis_script_runtime_unload(rt, i);
            inst->pending_unschedule = false;
        }
    }
}

void aegis_runtime_reset_idle(aegis_script_instance_t *inst) {
    inst->pending_unschedule = false;
    inst->idle_ticks = 0;
}

bool aegis_runtime_is_pending_unschedule(const aegis_script_instance_t *inst) {
    return inst->pending_unschedule;
}
