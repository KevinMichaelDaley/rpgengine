/**
 * @file aegis_topic_route.c
 * @brief Topic subscription and event routing: subscribe, unsubscribe, publish.
 *
 * 3 non-static functions: aegis_topic_subscribe, aegis_topic_unsubscribe,
 * aegis_topic_publish.
 */

#include "ferrum/aegis/aegis_event.h"

#include <string.h>

/* ----------------------------------------------------------------------- */
/* Subscribe                                                                */
/* ----------------------------------------------------------------------- */

bool aegis_topic_subscribe(aegis_topic_table_t *table,
                           uint32_t topic_hash,
                           uint32_t script_id) {
    /* Check for duplicate. */
    for (uint32_t i = 0; i < table->count; i++) {
        if (table->subs[i].topic_hash == topic_hash &&
            table->subs[i].script_id  == script_id) {
            return false; /* Already subscribed. */
        }
    }
    /* Check capacity. */
    if (table->count >= table->capacity) {
        return false;
    }
    table->subs[table->count].topic_hash = topic_hash;
    table->subs[table->count].script_id  = script_id;
    table->count++;
    return true;
}

/* ----------------------------------------------------------------------- */
/* Unsubscribe                                                              */
/* ----------------------------------------------------------------------- */

bool aegis_topic_unsubscribe(aegis_topic_table_t *table,
                             uint32_t topic_hash,
                             uint32_t script_id) {
    for (uint32_t i = 0; i < table->count; i++) {
        if (table->subs[i].topic_hash == topic_hash &&
            table->subs[i].script_id  == script_id) {
            /* Swap with last entry for O(1) removal. */
            table->count--;
            if (i < table->count) {
                table->subs[i] = table->subs[table->count];
            }
            return true;
        }
    }
    return false;
}

/* ----------------------------------------------------------------------- */
/* Publish (route event to all subscribers of its topic)                    */
/* ----------------------------------------------------------------------- */

void aegis_topic_publish(const aegis_topic_table_t *table,
                         const aegis_event_t *ev,
                         aegis_event_queue_t *queues,
                         uint32_t queue_count) {
    for (uint32_t i = 0; i < table->count; i++) {
        if (table->subs[i].topic_hash == ev->type) {
            uint32_t sid = table->subs[i].script_id;
            if (sid < queue_count) {
                aegis_event_queue_push(&queues[sid], ev);
            }
        }
    }
}
