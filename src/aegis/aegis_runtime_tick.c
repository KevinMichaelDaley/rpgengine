/**
 * @file aegis_runtime_tick.c
 * @brief Aegis script runtime: fiber dispatch and event publishing.
 *
 * The script fiber function is a long-lived loop:
 *   1. Pop event from instance's queue (busy-wait via job_yield if empty)
 *   2. Set vm->event and run VM
 *   3. On force-yield: job_yield() to let other fibers run, then resume
 *   4. On explicit yield: go back to step 1 for next event
 *   5. On exit/error: mark inactive and return (fiber dies)
 */

#include "ferrum/aegis/aegis_runtime.h"
#include "ferrum/job/system.h"
#include <string.h>

/* ----------------------------------------------------------------------- */
/* Script fiber function                                                    */
/* ----------------------------------------------------------------------- */

/**
 * @brief Long-lived fiber function for a single script instance.
 *
 * Runs for the lifetime of the script. Processes events from the
 * instance's queue, yielding the fiber on force-yield (fuel exhaustion)
 * and when waiting for events.
 */
static void script_fiber_fn(void *user_data) {
    aegis_script_instance_t *inst = (aegis_script_instance_t *)user_data;
    aegis_event_t ev;

    /* Main event loop: runs until script exits or errors. */
    for (;;) {
        /* Pop next event. If queue is empty, yield and retry.
         * The scheduler re-enqueues us immediately; we'll spin
         * briefly until the tick pushes more events. */
        if (!aegis_event_queue_pop(&inst->event_queue, &ev)) {
            /* No events pending. Yield to let other fibers run. */
            job_yield();

            /* Check if we were deactivated while yielded. */
            if (!inst->active) {
                return;
            }
            continue;
        }

        /* Attach event to VM and run. */
        inst->vm.event = &ev;

        for (;;) {
            aegis_vm_status_t status = aegis_vm_run(&inst->vm);

            switch (status) {
            case AEGIS_VM_YIELDED:
                /* Script explicitly yielded — done with this event.
                 * Go back to outer loop for next event. */
                goto next_event;

            case AEGIS_VM_FORCE_YIELDED:
                /* Fuel exhausted. Yield fiber to let other fibers run,
                 * then refuel and resume VM from same PC. */
                job_yield();
                if (!inst->active) {
                    return;
                }
                aegis_vm_reset_fuel(&inst->vm);
                /* Continue inner loop to resume VM. */
                break;

            case AEGIS_VM_WAIT_YIELDED:
                /* Async op pending. Yield fiber, then resume VM
                 * which will re-execute the wait/poll. */
                job_yield();
                if (!inst->active) {
                    return;
                }
                aegis_vm_reset_fuel(&inst->vm);
                break;

            case AEGIS_VM_EXITED:
                /* Script terminated normally. */
                inst->active = false;
                return;

            case AEGIS_VM_ERROR:
                /* Runtime error. */
                inst->active = false;
                return;
            }
        }

        next_event:
        /* Clear event pointer before processing next. */
        inst->vm.event = NULL;
    }
}

/* ----------------------------------------------------------------------- */
/* aegis_script_runtime_start                                               */
/* ----------------------------------------------------------------------- */

void aegis_script_runtime_start(aegis_script_runtime_t *rt,
                                uint32_t script_id,
                                struct job_system *sys) {
    if (script_id >= rt->instance_cap) return;

    aegis_script_instance_t *inst = &rt->instances[script_id];
    if (!inst->active) return;

    inst->job_sys = sys;

    /* Dispatch a single long-lived fiber for this script. */
    job_dispatch_named(sys, script_fiber_fn, inst, 0, NULL, inst->name);
}

/* ----------------------------------------------------------------------- */
/* aegis_script_runtime_publish                                             */
/* ----------------------------------------------------------------------- */

void aegis_script_runtime_publish(aegis_script_runtime_t *rt,
                                  const aegis_event_t *ev) {
    /* Build a temporary array of event queue pointers indexed by script_id.
     * The topic_publish function expects queues[script_id] to be the queue
     * for that script. We pass the queue array from the instance table. */

    /* aegis_topic_publish needs a flat array of queues where index = script_id.
     * We can't pass rt->instances directly because the queue is a member,
     * not the struct itself. So we use a stack-allocated pointer array. */

    /* Actually, topic_publish takes aegis_event_queue_t* and indexes by
     * script_id. We need to build an array where arr[i] = instances[i].event_queue.
     * But the queues aren't contiguous in memory (they're embedded in instances).
     *
     * The simplest fix: iterate subscribers manually. */

    /* Walk topic table and push directly to matching queues. */
    for (uint32_t i = 0; i < rt->topics.count; i++) {
        if (rt->topics.subs[i].topic_hash == ev->type) {
            uint32_t sid = rt->topics.subs[i].script_id;
            if (sid < rt->instance_cap && rt->instances[sid].active) {
                aegis_event_queue_push(&rt->instances[sid].event_queue, ev);
            }
        }
    }
}
