/**
 * @file edit_entity_version.c
 * @brief Entity version tracking — lifecycle and mutation.
 *
 * Non-static functions: 4 (init, destroy, stamp, tombstone).
 */

#include "ferrum/editor/edit_entity_version.h"

#include <stdlib.h>
#include <string.h>

bool edit_version_init(edit_version_state_t *state, uint32_t entity_capacity) {
    if (!state || entity_capacity == 0) return false;

    state->version = 0;
    state->entity_capacity = entity_capacity;
    state->tombstone_head = 0;
    state->tombstone_count = 0;
    state->tombstone_capacity = EDIT_VERSION_TOMBSTONE_CAP;

    state->entity_version = calloc(entity_capacity, sizeof(uint64_t));
    if (!state->entity_version) return false;

    state->tombstones = calloc(EDIT_VERSION_TOMBSTONE_CAP,
                                sizeof(edit_version_tombstone_t));
    if (!state->tombstones) {
        free(state->entity_version);
        state->entity_version = NULL;
        return false;
    }

    return true;
}

void edit_version_destroy(edit_version_state_t *state) {
    if (!state) return;
    free(state->entity_version);
    free(state->tombstones);
    state->entity_version = NULL;
    state->tombstones = NULL;
    state->entity_capacity = 0;
    state->tombstone_capacity = 0;
}

void edit_version_stamp(edit_version_state_t *state, uint32_t entity_id) {
    if (!state || entity_id >= state->entity_capacity) return;
    state->version++;
    state->entity_version[entity_id] = state->version;
}

void edit_version_tombstone(edit_version_state_t *state, uint32_t entity_id) {
    if (!state) return;
    state->version++;

    state->tombstones[state->tombstone_head].entity_id = entity_id;
    state->tombstones[state->tombstone_head].version = state->version;

    state->tombstone_head =
        (state->tombstone_head + 1) % state->tombstone_capacity;
    if (state->tombstone_count < state->tombstone_capacity) {
        state->tombstone_count++;
    }
}
