---
id: rpg-kg04
status: closed
deps: [rpg-llm02b]
links: [rpg-kg02, rpg-llm03]
created: 2026-05-03T20:00:00Z
type: bug
priority: 1
assignee: KMD
parent:
tags: [knowledge-graph, bug, global, null, integration]
---
# g_aegis_knowledge_graph Never Assigned — KG Tools Non-Functional

The global `npc_knowledge_graph_t *g_aegis_knowledge_graph` declared in `src/aegis/ops/aegis_ops_knowledge.c:29` is initialized to NULL and never assigned a non-NULL value anywhere in the codebase.

This means:
- `KNOWLEDGE_QUERY` always returns `"no graph bound"` (aegis_ops_knowledge.c:107-110)
- `KG_SHORTEST_PATH` calls `npc_kg_astar(NULL, ...)` which returns false (aegis_ops_kg_path.c:168)
- `RELATED_ENTITIES` calls `find_node_by_id(NULL, ...)` which returns NULL (aegis_ops_related.c:167)

All three tools are permanently disabled at runtime.

## Fix
The demo_server (rpg-llm03) should set `g_aegis_knowledge_graph` during initialization, pointing to the shared per-world knowledge graph. The NPC state manager (rpg-npc01) integration in `aegis_ops_knowledge.c` should resolve per-entity graphs when `g_npc_state_registry` is available, falling back to the global.

## Acceptance
- [ ] KNOWLEDGE_QUERY returns real facts when KG is bound
- [ ] KG_SHORTEST_PATH returns path chains
- [ ] RELATED_ENTITIES returns entity lists
- [ ] All three degrade gracefully (not crash) when KG is NULL
