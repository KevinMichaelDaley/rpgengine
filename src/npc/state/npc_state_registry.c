/**
 * @file npc_state_registry.c
 * @brief Maps entity_id → npc_state_t for AEGIS op resolution.
 *
 * Non-static functions (4 of 4 max):
 *   1. npc_state_registry_init
 *   2. npc_state_registry_find
 *   3. npc_state_registry_add
 *   4. npc_state_registry_destroy
 */

#include "ferrum/npc/npc_state_manager.h"
#include <stdlib.h>
#include <string.h>

#define NPC_REGISTRY_INITIAL_CAP 8

/* ------------------------------------------------------------------ */
/* Internal helpers                                                    */
/* ------------------------------------------------------------------ */

static bool registry_grow_(npc_state_registry_t *reg) {
    uint32_t new_cap = reg->cap * 2;
    if (new_cap < reg->cap) return false;
    npc_state_t **new_entries = (npc_state_t **)realloc(
        reg->entries, new_cap * sizeof(npc_state_t *));
    if (!new_entries) return false;
    memset(new_entries + reg->cap, 0,
           (new_cap - reg->cap) * sizeof(npc_state_t *));
    reg->entries = new_entries;
    reg->cap = new_cap;
    return true;
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

void npc_state_registry_init(npc_state_registry_t *reg) {
    if (!reg) return;
    memset(reg, 0, sizeof(*reg));
    reg->entries = (npc_state_t **)calloc(NPC_REGISTRY_INITIAL_CAP,
                                          sizeof(npc_state_t *));
    if (reg->entries) reg->cap = NPC_REGISTRY_INITIAL_CAP;
}

npc_state_t *npc_state_registry_find(const npc_state_registry_t *reg,
                                      uint32_t entity_id) {
    if (!reg || !reg->entries) return NULL;
    for (uint32_t i = 0; i < reg->count; i++) {
        if (reg->entries[i] &&
            reg->entries[i]->npc_id == (uint64_t)entity_id) {
            return reg->entries[i];
        }
    }
    return NULL;
}

bool npc_state_registry_add(npc_state_registry_t *reg, npc_state_t *state) {
    if (!reg || !state || !reg->entries) return false;
    if (reg->count >= reg->cap && !registry_grow_(reg)) return false;
    reg->entries[reg->count++] = state;
    return true;
}

void npc_state_registry_destroy(npc_state_registry_t *reg) {
    if (!reg) return;
    free(reg->entries);
    memset(reg, 0, sizeof(*reg));
}
