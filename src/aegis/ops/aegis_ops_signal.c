/**
 * @file aegis_ops_signal.c
 * @brief SIGNAL and SUBSCRIBE instruction handlers.
 *
 * SIGNAL: rate-limited event publishing via the topic table.
 * SUBSCRIBE: wire the calling script to a topic.
 *
 * Both write an integer status code to the destination register.
 */

#include "ferrum/aegis/aegis_ops_signal.h"
#include "ferrum/aegis/aegis_vm.h"
#include "ferrum/aegis/aegis_event.h"
#include "ferrum/aegis/aegis_decode.h"

#include <string.h>

/** Signal status codes written to destination register. */
#define SIGNAL_OK           0
#define SIGNAL_RATE_LIMITED  1
#define SIGNAL_INVALID_TOPIC 2
#define SIGNAL_QUEUE_FULL    3

/** Subscribe status codes. */
#define SUBSCRIBE_OK              0
#define SUBSCRIBE_ALREADY         1
#define SUBSCRIBE_TABLE_FULL      2

#include <time.h>

/**
 * @brief Get monotonic clock in microseconds.
 */
static uint64_t clock_monotonic_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

bool aegis_op_signal(aegis_vm_t *vm, const aegis_decode_result_t *d) {
    uint32_t topic_hash = vm->regs[d->raw_b].u32;

    /* Reject zero hash (invalid topic). */
    if (topic_hash == 0 || !vm->topic_table) {
        vm->regs[d->raw_a].i32 = SIGNAL_INVALID_TOPIC;
        return true;
    }

    /* Rate-limit check. */
    uint64_t now = clock_monotonic_us();
    if (vm->last_signal_time_us != 0 &&
        (now - vm->last_signal_time_us) < vm->signal_rate_limit_us) {
        vm->regs[d->raw_a].i32 = SIGNAL_RATE_LIMITED;
        return true;
    }

    /* Build event from payload register. */
    aegis_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = topic_hash;
    ev.source = vm->script_id;
    ev.payload_len = 16;
    memcpy(ev.payload, vm->regs[d->raw_c].bytes, 16);

    /* Publish to all subscribers. Use the publish callback if set
     * (runtime routes to per-instance queues). Fall back to direct
     * topic_publish for unit tests without a runtime. */
    if (vm->publish_fn) {
        vm->publish_fn(vm->publish_ctx, &ev);
    } else if (vm->event_queue) {
        aegis_topic_publish(vm->topic_table, &ev,
                            vm->event_queue, 1);
    }

    vm->last_signal_time_us = now;
    vm->regs[d->raw_a].i32 = SIGNAL_OK;
    return true;
}

bool aegis_op_subscribe(aegis_vm_t *vm, const aegis_decode_result_t *d) {
    uint32_t topic_hash = vm->regs[d->raw_b].u32;

    if (!vm->topic_table) {
        vm->regs[d->raw_a].i32 = SUBSCRIBE_TABLE_FULL;
        return true;
    }

    bool ok = aegis_topic_subscribe(vm->topic_table, topic_hash,
                                    vm->script_id);
    if (ok) {
        vm->regs[d->raw_a].i32 = SUBSCRIBE_OK;
    } else {
        /* Distinguish: already subscribed vs table full. */
        /* Check if already subscribed by scanning. */
        bool found = false;
        for (uint32_t i = 0; i < vm->topic_table->count; i++) {
            if (vm->topic_table->subs[i].topic_hash == topic_hash &&
                vm->topic_table->subs[i].script_id == vm->script_id) {
                found = true;
                break;
            }
        }
        vm->regs[d->raw_a].i32 = found ? SUBSCRIBE_ALREADY
                                        : SUBSCRIBE_TABLE_FULL;
    }
    return true;
}
