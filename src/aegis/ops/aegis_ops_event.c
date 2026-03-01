/**
 * @file aegis_ops_event.c
 * @brief Event access instruction handlers: event_type, event_src, event_field.
 *
 * Per ref/aegis_bytecode_spec.md §3.3.
 * 3 non-static functions.
 */

#include "ferrum/aegis/aegis_ops_event.h"

#include <string.h>

/* ----------------------------------------------------------------------- */
/* event_type: load event type hash into register                           */
/* ----------------------------------------------------------------------- */

bool aegis_op_event_type(aegis_register_t *dst,
                         const aegis_event_t *event) {
    if (!event) {
        return false;
    }
    memset(dst, 0, sizeof(*dst));
    dst->u32 = event->type;
    return true;
}

/* ----------------------------------------------------------------------- */
/* event_src: load source entity ID into register                           */
/* ----------------------------------------------------------------------- */

bool aegis_op_event_src(aegis_register_t *dst,
                        const aegis_event_t *event) {
    if (!event) {
        return false;
    }
    memset(dst, 0, sizeof(*dst));
    dst->u32 = event->source;
    return true;
}

/* ----------------------------------------------------------------------- */
/* event_field: load typed field from event payload (bounds-checked)        */
/* ----------------------------------------------------------------------- */

bool aegis_op_event_field(aegis_register_t *dst,
                          const aegis_event_t *event,
                          uint32_t offset,
                          uint32_t size) {
    if (!event) {
        return false;
    }
    /* Bounds check: offset + size must fit within payload. */
    if (size > 16 || offset + size > event->payload_len) {
        return false;
    }
    if (offset + size > AEGIS_EVENT_MAX_PAYLOAD) {
        return false;
    }
    memset(dst, 0, sizeof(*dst));
    memcpy(dst->bytes, &event->payload[offset], size);
    return true;
}
