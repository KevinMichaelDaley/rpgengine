/**
 * @file aegis_event_queue.c
 * @brief Per-script event queue (ring buffer with overflow drop).
 *
 * 4 non-static functions: init, destroy, push, pop.
 */

#include "ferrum/aegis/aegis_event.h"

#include <stdlib.h>
#include <string.h>

/* ----------------------------------------------------------------------- */
/* Init                                                                     */
/* ----------------------------------------------------------------------- */

void aegis_event_queue_init(aegis_event_queue_t *q, uint32_t capacity) {
    q->buf   = (aegis_event_t *)malloc(capacity * sizeof(aegis_event_t));
    q->cap   = capacity;
    q->head  = 0;
    q->tail  = 0;
    q->count = 0;
}

/* ----------------------------------------------------------------------- */
/* Destroy                                                                  */
/* ----------------------------------------------------------------------- */

void aegis_event_queue_destroy(aegis_event_queue_t *q) {
    free(q->buf);
    q->buf   = NULL;
    q->cap   = 0;
    q->head  = 0;
    q->tail  = 0;
    q->count = 0;
}

/* ----------------------------------------------------------------------- */
/* Push (drops oldest on overflow)                                          */
/* ----------------------------------------------------------------------- */

bool aegis_event_queue_push(aegis_event_queue_t *q, const aegis_event_t *ev) {
    if (q->count == q->cap) {
        /* Queue is full: drop oldest by advancing head. */
        q->head = (q->head + 1) % q->cap;
        q->count--;
    }
    memcpy(&q->buf[q->tail], ev, sizeof(aegis_event_t));
    q->tail = (q->tail + 1) % q->cap;
    q->count++;
    return true;
}

/* ----------------------------------------------------------------------- */
/* Pop                                                                      */
/* ----------------------------------------------------------------------- */

bool aegis_event_queue_pop(aegis_event_queue_t *q, aegis_event_t *out) {
    if (q->count == 0) {
        return false;
    }
    memcpy(out, &q->buf[q->head], sizeof(aegis_event_t));
    q->head = (q->head + 1) % q->cap;
    q->count--;
    return true;
}
