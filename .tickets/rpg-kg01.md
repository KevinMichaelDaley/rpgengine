---
id: rpg-kg01
status: closed
deps: [rpg-llm02b, rpg-nav04]
links: [rpg-kg02, rpg-kg03]
created: 2026-04-27T04:25:00Z
type: task
priority: 2
assignee: KMD
parent:
tags: [knowledge-graph, spatial, navigation, relations, reachability]
---
# Knowledge Graph: Spatial & Navigability Relations

Extend the per-NPC knowledge graph with built-in spatial and navigability relation types, and a mechanism to synchronize navigation/pathfinding results into the graph as reachability edges.

## Requirements

### 1. New Built-in Relations

Add the following to [`g_builtin_relations`](engine/src/npc/graph/npc_kg_init.c:14):

| Relation | Meaning |
|----------|---------|
| `near` | Entity is within short range (< 10 m) |
| `inside` | Entity is contained within another entity/room |
| `adjacent_to` | Entity shares a boundary (door, window, wall) |
| `visible_from` | Entity can be seen from this position |
| `reachable_from` | A valid path exists (updated by nav system) |
| `path_to` | Concrete waypoint path cached from last nav query |

### 2. Edge Flags

Extend [`npc_kg_edge_t`](engine/include/ferrum/npc/npc_knowledge_graph.h:33):

```c
typedef struct npc_kg_edge {
    uint64_t to_node_id;
    uint32_t relation_id;
    float    weight;
    uint64_t timestamp_us;
    uint32_t flags;      /* NEW */
} npc_kg_edge_t;
```

Flags:
- `NPC_KG_EDGE_REACHABLE` — path has been verified by navigation system
- `NPC_KG_EDGE_STALE` — path may be invalid (dynamic blocker added)
- `NPC_KG_EDGE_SPATIAL` — derived from spatial query, not social/memory

### 3. Reachability Sync API

```c
/**
 * @brief After a successful nav query, record reachability in the KG.
 *
 * Creates or updates a `reachable_from` / `path_to` edge between
 * the NPC's current location node and the target location/entity node.
 *
 * @param kg          NPC knowledge graph.
 * @param from_entity Entity ID of the start (usually self).
 * @param to_entity   Entity ID of the destination.
 * @param path_cost   Total cost from the pathfinder result.
 * @param waypoints   Optional cached waypoint array (for `path_to`).
 */
void npc_kg_set_reachable(npc_knowledge_graph_t *kg,
                          uint64_t from_entity,
                          uint64_t to_entity,
                          float path_cost,
                          const vec3_t *waypoints,
                          uint32_t waypoint_count);
```

- If navigation fails (unreachable), mark the edge `STALE` or remove it.
- If the path is partial, set `STALE` but keep the edge (NPC knows a route exists but it may be blocked).

### 4. Spatial Auto-Edges from SENSE_QUERY

When [`execute_sense_query_`](engine/src/aegis/aegis_async_execute.c:139) detects entities via proximity/LOS/audio, automatically insert/update spatial edges in the NPC's KG:

```c
/* In sense executor, after detecting entity E at distance D: */
npc_kg_upsert_spatial_edge(kg, self_id, E_id,
    (D < 10.0f) ? "near" : "visible_from",
    1.0f - (D / max_range));
```

These edges are tagged with `NPC_KG_EDGE_SPATIAL` and decay faster than social edges.

## Files to Create

- `src/npc/graph/npc_kg_spatial.c` — spatial relation helpers, auto-edge from sense
- `src/npc/graph/npc_kg_reachability.c` — nav result → KG edge sync

## Files to Modify

- `include/ferrum/npc/npc_knowledge_graph.h` — add edge flags, new relation IDs
- `src/npc/graph/npc_kg_init.c` — register new built-in relations

## Acceptance

- [ ] `npc_kg_relation_id("reachable_from")` returns a valid ID.
- [ ] After successful nav query, KG contains a `reachable_from` edge with `NPC_KG_EDGE_REACHABLE`.
- [ ] After dynamic blocker blocks the route, edge is marked `STALE`.
- [ ] SENSE_QUERY detection creates `near` or `visible_from` edge in KG.
- [ ] Spatial edges decay faster than social edges (separate lambda).
