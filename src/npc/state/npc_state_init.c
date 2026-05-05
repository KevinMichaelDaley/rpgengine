/**
 * @file npc_state_init.c
 * @brief NPC state allocation, init, and destroy.
 *
 * Non-static functions (2 of 4 max):
 *   1. npc_state_init
 *   2. npc_state_destroy
 */

#include "ferrum/npc/npc_state_manager.h"
#include <stdlib.h>
#include <string.h>

#define NPC_STATE_DEFAULT_SYSTEM_PROMPT \
    "You are an NPC in a simulated world."

void npc_state_init(npc_state_t *state, uint64_t npc_id) {
    if (!state) return;
    memset(state, 0, sizeof(*state));
    state->npc_id = npc_id;
    state->context_max_tokens = 4096;
    strncpy(state->system_prompt, NPC_STATE_DEFAULT_SYSTEM_PROMPT, 4095);
    state->system_prompt[4095] = '\0';
    state->active = true;
    npc_kg_init(&state->kg, 16, 4);
    npc_sense_awareness_init(&state->awareness, 8);
}

void npc_state_destroy(npc_state_t *state) {
    if (!state) return;
    free(state->context_buffer);
    state->context_buffer = NULL;
    npc_kg_destroy(&state->kg);
    npc_sense_awareness_destroy(&state->awareness);
    memset(state, 0, sizeof(*state));
}
