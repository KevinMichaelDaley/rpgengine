---
id: rpg-llm02
status: open
deps: [rpg-llm01]
links: [rpg-llm02a, rpg-llm02b, rpg-llm02c, rpg-llm02d, rpg-llm02e]
created: 2026-04-25T22:40:00Z
type: epic
priority: 1
assignee: KMD
parent:
tags: [aegis, llm, npc, ai, tools, sense, knowledge, combat, trade]
---
# AEGIS LLM Tool Integration: SENSE_QUERY, KNOWLEDGE_QUERY, and Action Tools

**Split into subtasks:**
- [rpg-llm02a](rpg-llm02a.md) — SENSE_QUERY Async Opcode + Executor
- [rpg-llm02b](rpg-llm02b.md) — KNOWLEDGE_QUERY + Per-NPC Knowledge Graph + FAISS
- [rpg-llm02c](rpg-llm02c.md) — Trade Tools (TRADE_INIT, TRADE_SELL, TRADE_BUY, TRADE_ACCEPT, TRADE_REJECT)
- [rpg-llm02d](rpg-llm02d.md) — Combat/Navigation Tools (DEFEND, ATTACK, FLEE, GOTO)
- [rpg-llm02e](rpg-llm02e.md) — VM Tool Action Opcode Framework + Whitelist + Integration

**Implementation order:** `rpg-llm02e` → (`rpg-llm02a`, `rpg-llm02b`) → (`rpg-llm02c`, `rpg-llm02d`)

Implement first-class tool-calling support for AEGIS NPC scripts. The LLM emits tool calls as JSON; the AEGIS VM parses, validates, and dispatches them as either async world queries or immediate sandboxed actions.

## Design Reference

See `design/aegis_tools_integration.md` (in parent repo) for full architecture.

## Requirements

### SENSE_QUERY (Async)
- Opcode: `AEGIS_OP_SENSE_QUERY = 0x4B`
- Task type: `AEGIS_TASK_SENSE_QUERY = 3`
- Two modes: targeted (specific entity) and full sweep (all in range)
- Detection types: LOS (raycast), proximity (overlap/spatial grid), audio (propagation graph placeholder), smell (wind cone placeholder)
- Approximation by distance: exact near, simplified medium, none far
- Automatic per-turn update before every LLM prompt
- Result: variable-length `aegis_sense_result_t` with entity + event lists

### KNOWLEDGE_QUERY (Immediate)
- Opcode: `AEGIS_OP_KNOWLEDGE_QUERY = 0x4C`
- Searches per-NPC knowledge graph (directed graph with embeddings)
- Supports faction-shared subgraphs via pointer
- Semantic search over node embeddings (brute-force for <10K nodes, FAISS later)
- Result: `aegis_knowledge_result_t` with ranked facts

### Action Tools (Immediate)
- Single opcode: `AEGIS_OP_TOOL_ACTION = 0x4D`
- Tool ID enum: TRADE_INIT=0, TRADE_SELL=1, TRADE_BUY=2, TRADE_ACCEPT=3, TRADE_REJECT=4, DEFEND=5, ATTACK=6, FLEE=7, GOTO=8, KNOWLEDGE_QUERY=9
- All validated by AEGIS VM before dispatch to engine
- TRADE_INIT / TRADE_SELL / TRADE_BUY / TRADE_ACCEPT / TRADE_REJECT: engine-managed barter state machine (see rpg-llm06)
- DEFEND/ATTACK/FLEE: range + combat state checks
- GOTO: nav mesh validity check

### Per-NPC Knowledge Graph
- Nodes: entities, events, facts, locations, concepts
- Edges: relations (saw_at, heard_from, owns, fears, etc.) with decay
- Auto-insert from SENSE_QUERY results without LLM tool call
- Shared subgraph pointer for faction/common knowledge

### Security
- Tool name whitelist (10 tools only)
- Range gating for all physical-world tools
- Fuel consumption on tool_action (50 per call)
- No raw ECS/physics/network access from scripts

## Files to Create

### Headers (≤2 types each)
- `include/ferrum/aegis/aegis_tools.h` — tool IDs + result types
- `include/ferrum/aegis/aegis_ops_tools.h` — opcode handler declarations
- `include/ferrum/npc/npc_knowledge_graph.h` — graph node/edge types
- `include/ferrum/npc/npc_sense.h` — sense result types
- `include/ferrum/npc/npc_trade.h` — barter state struct, phase enum, error codes
- `include/ferrum/npc/npc_combat_action.h` — combat action types

### Source (≤4 non-static functions per file)
- `src/aegis/ops/aegis_ops_sense.c` — sense_query opcode submit
- `src/aegis/ops/aegis_ops_tool.c` — tool_action dispatch
- `src/aegis/ops/aegis_ops_knowledge.c` — knowledge_query search
- `src/npc/graph/npc_kg_init.c` — graph init/destroy
- `src/npc/graph/npc_kg_insert.c` — node/edge insert
- `src/npc/graph/npc_kg_search.c` — semantic search
- `src/npc/graph/npc_kg_decay.c` — edge weight decay
- `src/npc/sense/npc_sense_auto.c` — per-turn auto update
- `src/npc/sense/npc_sense_execute.c` — sense query executor
- `src/npc/trade/npc_trade_init.c` — TRADE_INIT validation + state transition
- `src/npc/trade/npc_trade_sell.c` — TRADE_SELL in-loop + broadcast
- `src/npc/trade/npc_trade_buy.c` — TRADE_BUY in-loop + broadcast
- `src/npc/trade/npc_trade_resolve.c` — TRADE_ACCEPT, TRADE_REJECT, timeout, combat exit
- `src/npc/trade/npc_trade_prompt.c` — engine-generated barter prompt text
- `src/npc/combat/npc_combat_action.c` — DEFEND/ATTACK/FLEE validation
- `src/npc/nav/npc_nav_action.c` — GOTO validation

### Tests
- `tests/npc/npc_knowledge_graph_tests.c`
- `tests/npc/npc_sense_tests.c`
- `tests/npc/npc_trade_state_tests.c` — state machine transitions, combat exit, timeout
- `tests/aegis/aegis_tools_tests.c`

## Integration Points
1. `include/ferrum/aegis/aegis_types.h` — add opcodes
2. `include/ferrum/aegis/aegis_async.h` — add task type
3. `src/aegis/aegis_vm_run.c` — add cases to switch
4. `src/aegis/aegis_async_execute.c` — add sense_query executor
5. `Makefile` — add `src/npc/` wildcard to SRC_HEADLESS

## TDD Requirements
- Write all tests before implementation.
- Cover: graph insert/search/decay, sense query with mock physics, trade validation (range, language), combat action state checks, tool whitelist enforcement.
- Tests must compile with `libheadless.a` (no GL dependency).
