/**
 * @file aegis_topic_table.c
 * @brief Topic routing table: init, destroy, hash.
 *
 * 3 non-static functions: aegis_topic_hash, aegis_topic_table_init,
 * aegis_topic_table_destroy.
 */

#include "ferrum/aegis/aegis_event.h"

#include <stdlib.h>
#include <string.h>

/* ----------------------------------------------------------------------- */
/* Topic hash (FNV-1a 32-bit)                                               */
/* ----------------------------------------------------------------------- */

uint32_t aegis_topic_hash(const char *name) {
    uint32_t hash = 2166136261u; /* FNV offset basis */
    for (const char *p = name; *p; p++) {
        hash ^= (uint32_t)(uint8_t)*p;
        hash *= 16777619u; /* FNV prime */
    }
    /* Ensure non-zero. */
    if (hash == 0) {
        hash = 1;
    }
    return hash;
}

/* ----------------------------------------------------------------------- */
/* Init                                                                     */
/* ----------------------------------------------------------------------- */

void aegis_topic_table_init(aegis_topic_table_t *table,
                            uint32_t max_subs,
                            uint32_t max_scripts) {
    (void)max_scripts; /* Reserved for future use. */
    table->subs     = (aegis_topic_sub_t *)malloc(
                          max_subs * sizeof(aegis_topic_sub_t));
    table->count    = 0;
    table->capacity = max_subs;
}

/* ----------------------------------------------------------------------- */
/* Destroy                                                                  */
/* ----------------------------------------------------------------------- */

void aegis_topic_table_destroy(aegis_topic_table_t *table) {
    free(table->subs);
    table->subs     = NULL;
    table->count    = 0;
    table->capacity = 0;
}
