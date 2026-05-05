---
id: rpg-llm03
status: closed
deps: [rpg-llm02]
links: [rpg-llm09, rpg-npc01]
created: 2026-04-25T22:50:00Z
type: task
priority: 1
assignee: KMD
parent: 
tags: [aegis, llm, npc, demo, faiss, physics, integration, e2e]
---
# Full LLM Tool Round-Trip: Live NPC Demo in demo_server

End-to-end integration of the complete AEGIS LLM tool chain into `demo_server`. An NPC fiber runs a full turn: auto-sense → build prompt → LLM call → parse tool_calls → dispatch SENSE_QUERY / KNOWLEDGE_QUERY / TRADE / DEFEND / ATTACK / FLEE / GOTO → verify engine events — all against real physics, real ECS, and live Ollama/FAISS.

## Prerequisites
- rpg-llm01 (LLM prompt opcode + cost tracker) — CLOSED
- rpg-llm02 (tool opcodes + knowledge graph + FAISS) — must be complete

## Goals

1. **Live NPC in demo_server**: At least one NPC entity spawned in the physics world with an AEGIS script that runs the full LLM turn loop.
2. **Real physics SENSE_QUERY**: The NPC's auto-sense and manual SENSE_QUERY tool calls hit the actual `phys_world` with raycasts, overlap queries, and spatial grid lookups.
3. **Live LLM**: Prompts go to Ollama (or configured provider) and return real tool_calls. The LLM is given the awareness list + memory context in the prompt.
4. **FAISS-backed KNOWLEDGE_QUERY**: The NPC's knowledge graph uses the FAISS C++ wrapper for semantic search over node embeddings. No brute-force fallback.
5. **Tool dispatch to engine**: TRADE, DEFEND, ATTACK, FLEE, GOTO tool calls build update events and signal them into the engine's event queue, triggering real behavior tree / locomotion changes.
6. **Observable**: Server logs show each turn: sense results, prompt tokens, LLM response, parsed tools, validation results, engine actions.

## Demo Flow

```
[demo_server tick]
  │
  ▼
[For each NPC with AEGIS script]
  │
  ├── auto_sense() ──► physics world query ──► awareness list
  │                      (raycast, overlap, spatial grid)
  │
  ├── build_prompt() ──► serialize awareness + top-5 memories
  │
  ├── llm_prompt() ──► async HTTP POST to Ollama
  │                      (wait-yield fiber)
  │
  ├── parse_response() ──► extract content + tool_calls[]
  │
  ├── dispatch_tools()
  │   ├── SENSE_QUERY  ──► async physics query ──► wait
  │   ├── KNOWLEDGE_QUERY ──► FAISS search ──► facts
  │   ├── TRADE        ──► range+lang validate ──► SIGNAL trade_event
  │   ├── DEFEND/ATTACK/FLEE/GOTO ──► validate ──► SIGNAL combat/nav_event
  │
  └── engine processes events ──► BT/locomotion updates
```

## Components to Integrate

### FAISS Build Integration
- Add FAISS as a git submodule or system dependency.
- Build `libfaiss.a` (CPU-only, `-DFAISS_ENABLE_GPU=OFF`).
- Compile `src/npc/graph/npc_kg_faiss_wrapper.cpp` against FAISS headers.
- Link `libfaiss.a` into `libheadless.a` and `demo_server`.
- Makefile target: `make faiss` builds the dependency once.

### demo_server Extension
- `tests/examples/demo_server.c` already sets up physics world + net runtime.
- Add NPC spawn: create a physics body + collider + AEGIS script component.
- Add NPC tick: per-tick fiber scheduler that resumes NPC scripts.
- Add NPC LLM client: reuse `llm_client` from rpg-llm01, configure via engine settings.
- Add NPC memory init: create `npc_knowledge_graph_t` on spawn, link to faction shared graph.

### NPC AEGIS Script (Example)
```asm
.topic !npc_turn

resume
; 1. Auto-sense (engine injects this before script runs)
;    Awareness list is pre-loaded into static memory.

; 2. Build prompt and call LLM
llm_prompt r0, r_prompt_off, r_max_tok
wait r10, r11, r0

; 3. Parse result — tool_calls are in r10 heap buffer
;    (VM helper opcodes or script logic iterates tool_calls)

; 4. Dispatch each tool
;    SENSE_QUERY example:
sense_query r20, r_mode_flags, r_target
wait r21, r22, r20

;    KNOWLEDGE_QUERY example:
knowledge_query r30, r_keyphrase_off

;    TRADE example:
load_imm r_tool_id, 0       ; TRADE
load_imm r_args, trade_json ; heap offset
tool_action r_result, r_tool_id, r_args

; 5. Yield until next tick
yield
```

## Acceptance Criteria

- [ ] `make demo_server` builds successfully with FAISS linked.
- [ ] Running `./build/demo_server` spawns at least one NPC.
- [ ] NPC log shows: `SENSE: detected N entities` every tick.
- [ ] NPC log shows: `LLM: prompt_tokens=X completion_tokens=Y` after LLM round-trip.
- [ ] NPC log shows parsed tool calls (e.g., `TOOL: SENSE_QUERY target=player_42`).
- [ ] NPC log shows engine events generated (e.g., `EVENT: TRADE_PROPOSED`).
- [ ] KNOWLEDGE_QUERY returns facts from FAISS (not error/empty).
- [ ] SENSE_QUERY returns real physics hits (not mock data).
- [ ] No crashes after 60 seconds of continuous ticks (target: 60 Hz).

## Files to Modify
- `tests/examples/demo_server.c` — add NPC spawn, tick, and LLM integration
- `Makefile` — add FAISS build rules and link flags
- `engine/package.json` or build script — document FAISS dependency

## Files to Create
- `src/npc/demo/npc_demo_spawn.c` — NPC spawn helper
- `src/npc/demo/npc_demo_tick.c` — per-tick NPC scheduler
- `src/npc/graph/npc_kg_faiss_wrapper.cpp` — FAISS C wrapper
- `tests/npc/npc_faiss_tests.c` — FAISS index create/add/search/destroy tests
- `tests/examples/npc_demo_integration_tests.c` — Full turn test with mock LLM

## Notes
- Use `qwen2.5-coder:1.5b` as the default LLM for the demo (proven working in rpg-llm01 smoke test).
- FAISS flat index (IndexFlatIP or IndexFlatL2) is sufficient for the demo; IVF/HSW can be added later.
- If Ollama is not running, the demo_server should degrade gracefully: skip LLM turn, retry next tick.
