/**
 * @file aegis_ops_await.c
 * @brief AWAIT_EVENT instruction handler.
 *
 * Scans the script's event queue for an event matching the requested
 * topic hash. If found, pops it and packs type + source + first 8
 * payload bytes into the destination register. If no match, returns
 * false to trigger a wait-yield.
 *
 * The scan is destructive only for the matched event. Non-matching
 * events are re-enqueued in order to preserve queue integrity.
 */

#include "ferrum/aegis/aegis_ops_signal.h"
#include "ferrum/aegis/aegis_vm.h"
#include "ferrum/aegis/aegis_event.h"
#include "ferrum/aegis/aegis_decode.h"

#include <string.h>

bool aegis_op_await_event(aegis_vm_t *vm, const aegis_decode_result_t *d) {
    if (!vm->event_queue) {
        return false;
    }

    uint32_t topic_hash = vm->regs[d->raw_b].u32;
    uint32_t count = aegis_event_queue_count(vm->event_queue);

    if (count == 0) {
        return false;
    }

    /* Scan queue for matching event. Pop and re-push non-matching ones. */
    aegis_event_t ev;
    bool found = false;
    aegis_event_t matched;

    for (uint32_t i = 0; i < count; i++) {
        if (!aegis_event_queue_pop(vm->event_queue, &ev)) {
            break;
        }
        if (!found && ev.type == topic_hash) {
            found = true;
            matched = ev;
            /* Don't re-push — this event is consumed. */
        } else {
            aegis_event_queue_push(vm->event_queue, &ev);
        }
    }

    if (!found) {
        return false; /* No match → wait-yield. */
    }

    /* Pack result into destination register:
     * bytes[0..3]  = type (u32)
     * bytes[4..7]  = source (u32)
     * bytes[8..15] = first 8 bytes of payload
     */
    memset(vm->regs[d->raw_a].bytes, 0, 16);
    memcpy(vm->regs[d->raw_a].bytes, &matched.type, 4);
    memcpy(vm->regs[d->raw_a].bytes + 4, &matched.source, 4);
    uint32_t copy_len = matched.payload_len < 8 ? matched.payload_len : 8;
    if (copy_len > 0) {
        memcpy(vm->regs[d->raw_a].bytes + 8, matched.payload, copy_len);
    }

    return true;
}
