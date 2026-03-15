/**
 * @file edit_entity_version_query.c
 * @brief Entity version queries — delta sync support.
 *
 * Non-static functions: 3 (needs_full_resync, count_changed, get_changed_ids).
 */

#include "ferrum/editor/edit_entity_version.h"

bool edit_version_needs_full_resync(const edit_version_state_t *state,
                                     uint64_t since_version) {
    if (!state) return true;

    /* Never synced — must do full. */
    if (since_version == 0) return true;

    /* If tombstone ring hasn't filled, no entries were lost. */
    if (state->tombstone_count < state->tombstone_capacity) return false;

    /* Ring is full. Find the oldest tombstone (the one at tombstone_head,
     * which is the next to be overwritten = the oldest surviving entry). */
    uint32_t oldest_idx = state->tombstone_head; /* points at oldest when full */
    uint64_t oldest_version = state->tombstones[oldest_idx].version;

    /* If the oldest surviving tombstone has version > since_version,
     * then some tombstones between since_version and oldest were lost. */
    return oldest_version > since_version;
}

uint32_t edit_version_count_changed(const edit_version_state_t *state,
                                     uint64_t since_version) {
    if (!state) return 0;

    uint32_t count = 0;
    for (uint32_t i = 0; i < state->entity_capacity; ++i) {
        if (state->entity_version[i] > since_version) {
            count++;
        }
    }
    return count;
}

uint32_t edit_version_get_changed_ids(const edit_version_state_t *state,
                                       uint64_t since_version,
                                       uint32_t *out_ids, uint32_t max_ids) {
    if (!state || !out_ids || max_ids == 0) return 0;

    uint32_t count = 0;
    for (uint32_t i = 0; i < state->entity_capacity && count < max_ids; ++i) {
        if (state->entity_version[i] > since_version) {
            out_ids[count++] = i;
        }
    }
    return count;
}
