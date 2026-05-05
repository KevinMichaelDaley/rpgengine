---
id: rpg-kg02
status: closed
deps: [rpg-llm02b]
links: [rpg-kg01, rpg-kg03, rpg-kg04, rpg-kg05, rpg-kg06]
created: 2026-04-27T04:25:00Z
type: task
priority: 2
assignee: KMD
parent:
tags: [knowledge-graph, traversal, astar, pathfinding, related-entities]
---
# Knowledge Graph Traversal: A* Pathfinding Over the KG

Implement graph traversal operations on the per-NPC knowledge graph. The NPC should be able to ask "who is connected to X?" and "what is the shortest chain of trust from me to Y?"

## Requirements

### 1. A* on the Knowledge Graph

```c
typedef struct npc_kg_path_request {
    uint64_t start_node_id;
    uint64_t goal_node_id;
    /** @brief Whitelist of traversable relation IDs.
     *
     *  If @p allowed_relation_count is 0, all relations are traversable.
     *  Otherwise only edges whose relation_id matches one of the IDs
     *  in this array are considered during expansion.
     *
     *  This allows multi-hop queries such as
     *  "find a path using only 'trusts' OR 'works_for' edges".
     */
    uint32_t allowed_relations[8];
    uint32_t allowed_relation_count; /**< 0 = any relation allowed */
    float    max_cost;
} npc_kg_path_request_t;

typedef struct npc_kg_path_result {
    uint32_t step_count;     /* number of edges in path */
    uint64_t *node_ids;      /* arena-allocated, step_count+1 entries */
    uint32_t *relation_ids;  /* arena-allocated, step_count entries */
    float    total_cost;
    bool     found;
} npc_kg_path_result_t;

/**
 * @brief A* search through the knowledge graph.
 *
 * Heuristic: inverse edge weight (higher weight = closer connection).
 * If @p allowed_relation_count > 0, only edges whose relation_id is present
 * in @p allowed_relations[] are traversable.
 *
 * @param kg      Knowledge graph.
 * @param req     Path request.
 * @param result  Out path result (node_ids/relation_ids arena-allocated).
 * @return true if a path was found.
 */
bool npc_kg_astar(const npc_knowledge_graph_t *kg,
                  const npc_kg_path_request_t *req,
                  npc_kg_path_result_t *result);
```

### 2. Single-Argument Tool: `RELATED_ENTITIES`

Add new tool ID to [`aegis_tool_id_t`](engine/include/ferrum/aegis/aegis_tools.h:23):

```c
AEGIS_TOOL_RELATED_ENTITIES = 10
```

JSON argument:
```json
{"entity": "Alice", "relation": "trusts"}
```

Behavior:
1. Resolve `entity` string → node_id in the NPC's KG (by name attribute or FAISS lookup).
2. Find all outgoing edges from that node matching `relation`.
3. Return a list of `(relation, target_entity, weight)` tuples.

Result layout (heap arena):
```c
typedef struct aegis_related_entities_result {
    int32_t  status;
    uint32_t count;
    /* Followed by count × aegis_related_entity_t */
} aegis_related_entities_result_t;

typedef struct aegis_related_entity {
    uint32_t relation_id;
    uint64_t entity_id;
    float    weight;
    char     name[32];
} aegis_related_entity_t;
```

### 3. Single-Argument Tool: `KG_SHORTEST_PATH`

Add new tool ID:
```c
AEGIS_TOOL_KG_SHORTEST_PATH = 11
```

JSON argument:
```json
{"target": "Bob"}
```

Behavior:
1. Start node = NPC's self node (entity_id stored in VM).
2. Goal node = resolve `target` string → node_id.
3. Run [`npc_kg_astar()`](engine/src/npc/graph/npc_kg_astar.c:1) with `allowed_relation_count = 0` (any relation).
4. Return the chain of entities and relations.

Result layout:
```c
typedef struct aegis_kg_path_result {
    int32_t  status;        /* 0 = found, -1 = unreachable */
    uint32_t step_count;
    /* Followed by step_count × aegis_kg_path_step_t */
} aegis_kg_path_result_t;

typedef struct aegis_kg_path_step {
    uint32_t relation_id;
    uint64_t to_entity_id;
    char     relation_name[32];
    char     entity_name[32];
} aegis_kg_path_step_t;
```

## Files to Create

- `src/npc/graph/npc_kg_astar.c` — A* implementation on the knowledge graph (≤4 non-static functions)
- `src/aegis/ops/aegis_ops_related.c` — `RELATED_ENTITIES` tool handler
- `src/aegis/ops/aegis_ops_kg_path.c` — `KG_SHORTEST_PATH` tool handler
- `tests/npc/npc_kg_astar_tests.c` — A* correctness tests

## Files to Modify

- `include/ferrum/aegis/aegis_tools.h` — add tool IDs 10 and 11
- `include/ferrum/npc/npc_knowledge_graph.h` — add path request/result types
- `src/aegis/ops/aegis_ops_tool.c` — wire tool IDs 10, 11 into dispatch

## Acceptance

- [ ] A* finds shortest trust chain across 5 nodes.
- [ ] A* respects `allowed_relations[]` whitelist (single and multiple).
- [ ] `RELATED_ENTITIES` returns all nodes linked by "trusts" from Alice.
- [ ] `KG_SHORTEST_PATH` returns chain of 3 steps from NPC to Bob.
- [ ] Unreachable target returns status = -1 with empty steps.
- [ ] Tool handlers use `json_parse` for argument parsing.
