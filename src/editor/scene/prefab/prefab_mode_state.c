/**
 * @file prefab_mode_state.c
 * @brief Prefab mode state lifecycle functions.
 *
 * Non-static functions (2 / 4 limit):
 *   prefab_mode_state_init
 *   prefab_mode_state_reset
 */

#include "ferrum/editor/scene/prefab/prefab_mode_state.h"

#include <string.h>

void prefab_mode_state_init(prefab_mode_state_t *state) {
    if (!state) return;
    memset(state, 0, sizeof(*state));
    state->root_entity_id = UINT32_MAX;
}

void prefab_mode_state_reset(prefab_mode_state_t *state) {
    if (!state) return;
    memset(state, 0, sizeof(*state));
    state->root_entity_id = UINT32_MAX;
}
