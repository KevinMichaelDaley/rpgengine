/**
 * @file npc_state_manager.h
 * @brief Per-NPC state: knowledge graph, context buffer, prompt assembly.
 *
 * Each NPC with an AEGIS script owns one npc_state_t. The registry maps
 * entity_id → npc_state_t so AEGIS ops can resolve the correct KG and
 * context without the script knowing which NPC it belongs to.
 */

#ifndef FERRUM_NPC_STATE_MANAGER_H
#define FERRUM_NPC_STATE_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "ferrum/npc/npc_knowledge_graph.h"
#include "ferrum/npc/npc_sense.h"

/* =================================================================== */
/* Per-NPC state                                                        */
/* =================================================================== */

typedef struct npc_state {
    uint64_t                    npc_id;
    npc_knowledge_graph_t       kg;
    struct npc_knowledge_graph *shared_kg;
    npc_sense_awareness_t       awareness;

    char    *context_buffer;
    uint32_t context_len;
    uint32_t context_cap;
    uint32_t context_token_estimate;
    uint32_t context_max_tokens;

    char    system_prompt[4096];
    char    statblock[2048];
    char    status_line[512];

    uint16_t tool_whitelist;
    uint16_t tool_fuel_budget;

    bool     active;
    bool     context_dirty;
} npc_state_t;

/* =================================================================== */
/* Registry                                                             */
/* =================================================================== */

typedef struct npc_state_registry {
    npc_state_t **entries;
    uint32_t      count;
    uint32_t      cap;
} npc_state_registry_t;

/* =================================================================== */
/* Lifecycle                                                            */
/* =================================================================== */

void npc_state_init(npc_state_t *state, uint64_t npc_id);
void npc_state_destroy(npc_state_t *state);

/* =================================================================== */
/* Context compaction                                                   */
/* =================================================================== */

bool npc_state_compact(npc_state_t *npc);

/* =================================================================== */
/* Prompt assembly                                                      */
/* =================================================================== */

char *npc_state_prompt_assemble(const npc_state_t *npc,
                                const char *user_message);

/* =================================================================== */
/* KG pre-population                                                    */
/* =================================================================== */

uint32_t npc_state_kg_prepopulate(npc_state_t *npc,
                                  const npc_knowledge_graph_t *source);

/* =================================================================== */
/* Registry management                                                  */
/* =================================================================== */

void npc_state_registry_init(npc_state_registry_t *reg);
npc_state_t *npc_state_registry_find(const npc_state_registry_t *reg,
                                      uint32_t entity_id);
bool npc_state_registry_add(npc_state_registry_t *reg, npc_state_t *state);
void npc_state_registry_destroy(npc_state_registry_t *reg);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_NPC_STATE_MANAGER_H */
