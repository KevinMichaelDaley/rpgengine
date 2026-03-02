/**
 * @file phys_pair_set_gc.c
 * @brief Maintenance operations for the body pair hash set.
 *
 * Implements clear, prune_before, count.
 */
#include "ferrum/physics/phys_pair_set.h"
#include <stdlib.h>
#include <string.h>

void phys_pair_set_clear(phys_pair_set_t *set) {
    if (!set || !set->entries) return;
    memset(set->entries, 0, set->capacity * sizeof(phys_pair_entry_t));
    set->count = 0;
}

void phys_pair_set_prune_before(phys_pair_set_t *set, uint32_t min_tick) {
    if (!set || !set->entries || set->count == 0) return;

    /*
     * Collect surviving entries into a temporary buffer, then
     * rebuild the table. This avoids broken probe chains from
     * in-place deletion with linear probing.
     */
    uint32_t cap = set->capacity;
    uint32_t surviving = 0;

    /* Count survivors first. */
    for (uint32_t i = 0; i < cap; i++) {
        if (set->entries[i].occupied && set->entries[i].last_tick >= min_tick) {
            surviving++;
        }
    }

    if (surviving == set->count) return; /* nothing to prune */

    if (surviving == 0) {
        phys_pair_set_clear(set);
        return;
    }

    /* Copy survivors to a temporary heap buffer, clear table, re-insert. */
    phys_pair_entry_t *tmp = (phys_pair_entry_t *)malloc(
        surviving * sizeof(phys_pair_entry_t));
    if (!tmp) return; /* allocation failure — leave table unchanged */

    uint32_t j = 0;
    for (uint32_t i = 0; i < cap && j < surviving; i++) {
        if (set->entries[i].occupied && set->entries[i].last_tick >= min_tick) {
            tmp[j++] = set->entries[i];
        }
    }

    /* Clear table and re-insert survivors. */
    memset(set->entries, 0, cap * sizeof(phys_pair_entry_t));
    set->count = 0;
    for (uint32_t i = 0; i < surviving; i++) {
        phys_pair_set_upsert(set, tmp[i].pair_key, tmp[i].last_tick);
    }
    free(tmp);
}

uint32_t phys_pair_set_count(const phys_pair_set_t *set) {
    return set ? set->count : 0;
}
