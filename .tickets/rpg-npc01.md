---
id: rpg-npc01
status: closed
deps: [rpg-llm02b]
links: [rpg-llm03, rpg-llm02, rpg-llm09, rpg-npc02, rpg-npc03, rpg-npc04]
created: 2026-04-27T06:54:07Z
type: task
priority: 1
assignee: KMD
parent: rpg-llm03
tags: [npc, state, knowledge-graph, context, memory, compaction, aegis, llm]
---
# NPC State Manager: Per-NPC Knowledge Graph Store and Context Compaction

Each NPC maintains its own persistent reasoning context (prompt history, conversation state, emotional state, goals) and its own knowledge graph. The NPC State Manager owns the lifecycle of these per-NPC stores, exposes them to AEGIS scripts so that `KNOWLEDGE_QUERY` and `LLM_PROMPT` automatically target the correct NPC instance, and manages context compaction to stay within token budgets.

## Requirements

### 1. Per-NPC State Structure

```c
typedef struct npc_state {
    uint64_t                    npc_id;       /**< Unique NPC identifier. */
    npc_knowledge_graph_t       kg;           /**< Personal knowledge graph. */
    struct npc_knowledge_graph *shared_kg;    /**< Faction / common subgraph. */
    npc_sense_awareness_t       awareness;    /**< Currently sensed entities. */

    /* LLM context management. */
    char    *context_buffer;                  /**< Raw conversation/memory text. */
    uint32_t context_len;                     /**< Used bytes. */
    uint32_t context_cap;                     /**< Allocated capacity. */
    uint32_t context_token_estimate;          /**< Approximate token count. */
    uint32_t context_max_tokens;              /**< Budget ceiling. */

    /* Prompt assembly. */
    char    system_prompt[4096];              /**< Fixed lore + instructions. */
    char    statblock[2048];                  /**< JSON stat block. */
    char    status_line[512];                 /**< Current HP, stamina, location. */

    /* Tool whitelist (bitmask of aegis_tool_id_t). */
    uint16_t tool_whitelist;                  /**< Which tools this NPC may use. */
    uint16_t tool_fuel_budget;                /**< Per-turn fuel for tool calls. */

    /* State flags. */
    bool     active;                          /**< Currently running prompt loop. */
    bool     context_dirty;                   /**< Context needs compaction. */
} npc_state_t;
```

- **One `npc_state_t` per NPC with an AEGIS script.**
- Stateless NPCs (merchants, guards) get a minimal KG only.
- Major NPCs (companions, bosses) get full KG + persistent context.

### 2. Context Compaction

When `context_token_estimate > context_max_tokens`, compact before the next LLM call:

1. **Summarize**: Feed oldest N messages to a cheap local model for "summarize this conversation" → insert summary as a single system note.
2. **Truncate**: Remove the now-summarized messages from the context buffer.
3. **KG extract**: Parse the summary for key facts (names, locations, events) and auto-insert into the NPC's knowledge graph.
4. **Update estimates**: Recompute `context_token_estimate` via the cost tracker.

```c
bool npc_state_compact(npc_state_t *npc);
```

- Called automatically before `LLM_PROMPT` if `context_dirty == true`.
- Compaction must be deterministic (canonical form) so that AEGIS scripts see the same context after restart.
- Maximum compaction time: < 500 ms (done on background fiber, not the main thread).

### 3. AEGIS Integration

The NPC State Manager must be accessible from within AEGIS scripts **without** the script knowing which NPC it belongs to — the engine resolves `npc_state_t` from `vm->entity_id` at dispatch time.

Required adjustments to AEGIS:

- **`KNOWLEDGE_QUERY`**: When `aegis_op_knowledge_query()` is called, map `vm->entity_id` → `npc_state_t *` via a global registry. Use `npc_state->kg` (and optionally `npc_state->shared_kg`) as the search target instead of the global `g_aegis_knowledge_graph`.
- **`LLM_PROMPT`**: When `aegis_op_llm_prompt()` assembles the prompt, inject `npc_state->system_prompt`, `npc_state->statblock`, `npc_state->status_line`, and the top-N awareness entries from `npc_state->awareness`. Append `npc_state->context_buffer` as the chat history.
- **`AEGIS_OP_TOOL_ACTION`**: Gate on `npc_state->tool_whitelist`; deduct fuel from `npc_state->tool_fuel_budget`.

```c
/* Registry: maps entity_id → npc_state_t *. */
typedef struct npc_state_registry {
    npc_state_t **entries;
    uint32_t      count;
    uint32_t      cap;
} npc_state_registry_t;

npc_state_t *npc_state_registry_find(const npc_state_registry_t *reg,
                                      uint32_t entity_id);
```

- Registry is populated on NPC spawn, cleared on despawn.
- NULL-safe: if no state for an entity, `KNOWLEDGE_QUERY` falls back to global graph (backward compatible).

### 4. Prompt Assembly Pipeline

Before each `LLM_PROMPT`:

1. Begin with `npc_state->system_prompt` (lore + instructions).
2. Append `npc_state->statblock` as JSON.
3. Append `npc_state->status_line` (current HP, location, stance).
4. Generate awareness summary from `npc_state->awareness`: top-5 entities by salience, with name, distance, flags.
5. Generate memory summary: top-5 KG facts ranked by relevance to current query (or most recent).
6. Append `npc_state->context_buffer` (chat history).
7. Append the user's latest message / turn trigger.
8. The resulting prompt must not exceed `npc_state->context_max_tokens` (compact if needed).

### 5. Knowledge Graph Initialization

When an NPC spawns:

1. Create `npc_state_t`, init `kg` with `npc_kg_init()`.
2. Pre-populate `kg` from the **shared spatial knowledge graph** (rpg-llm03): the NPC knows the layout of their spawn region, key NPC names, faction relations.
3. Pre-populate from **lore dump**: biography facts, origin location, faction affiliation, known enemies.
4. Link `shared_kg` pointer to the faction subgraph.
5. The shared spatial KG is precomputed once per map load (rpg-llm03) and shared by all NPCs.

## Files to Create

- `include/ferrum/npc/npc_state_manager.h` — npc_state_t, registry, compaction API
- `src/npc/state/npc_state_init.c` — per-NPC state allocation + init
- `src/npc/state/npc_state_compact.c` — context compaction (summarize + truncate + KG extract)
- `src/npc/state/npc_state_registry.c` — entity_id → npc_state_t lookup
- `src/npc/state/npc_state_prompt.c` — full prompt assembly from state
- `src/npc/state/npc_state_kg_init.c` — pre-populate KG from shared map KG + lore dump
- `tests/npc/npc_state_tests.c` — init, compact, registry lookup, prompt assembly

## Files to Modify

- `src/aegis/ops/aegis_ops_knowledge.c` — resolve kg from npc_state via entity_id
- `src/aegis/ops/aegis_ops_llm.c` — resolve context from npc_state via entity_id
- `src/aegis/ops/aegis_ops_tool.c` — gate on tool_whitelist, deduct fuel_budget
- `src/aegis/aegis_runtime_init.c` — wire npc_state_registry into VM runtime
- `Makefile` — add `src/npc/state/` to NPC_SRC wildcard

## Acceptance

- [ ] `npc_state_init()` creates a state with empty KG, empty context, default system prompt.
- [ ] `npc_state_compact()` reduces context size by > 50% when over budget, preserving key facts.
- [ ] Compaction extracts new KG facts from the summary.
- [ ] `npc_state_registry_find()` returns correct npc_state_t for a given entity_id.
- [ ] `npc_state_prompt_assemble()` produces a prompt under `context_max_tokens`.
- [ ] AEGIS `KNOWLEDGE_QUERY` searches the correct per-NPC KG when entity has state.
- [ ] AEGIS `KNOWLEDGE_QUERY` falls back to global graph when entity has no state.
- [ ] Tool whitelist prevents NPC from calling tools it doesn't have access to.
- [ ] Context survives across multiple `LLM_PROMPT` rounds (persistent chat history).
- [ ] Registry survives NPC despawn/respawn cycle without leaking.

## Blockers

- rpg-llm02b [closed] KNOWLEDGE_QUERY + Per-NPC Knowledge Graph + FAISS Wrapper
- rpg-llm02a [closed] SENSE_QUERY Async Opcode + Executor

## Blocking

- rpg-llm03 [open] Full LLM Tool Round-Trip: Live NPC Demo in demo_server
