---
id: rpg-kg05
status: open
deps: [rpg-kg02]
links: [rpg-kg02]
created: 2026-05-03T20:00:00Z
type: bug
priority: 3
assignee: KMD
parent:
tags: [knowledge-graph, bug, performance, astar, lookup]
---
# npc_kg_astar O(n²) Neighbor Lookup Per Edge Expansion

In `npc_kg_astar` at `src/npc/graph/npc_kg_astar.c:203-208`, for every edge expansion, the code does a linear scan of all nodes to find the target:
```c
for (uint32_t k = 0; k < kg->node_count; k++) {
    if (kg->nodes[k].node_id == e->to_node_id) ...
}
```
For a graph with 1000 nodes and average degree 5, this is 5000 comparisons per expanded node. O(V × E) overall.

## Fix
Option 1: Build a hash table or sorted index from `node_id` → array index at graph init time.
Option 2: Store array indices in edges instead of `node_id` (requires invariant that nodes are never reordered).
Option 3: Accept O(n²) for small graphs but add a build-time index for graphs > N nodes.

## Acceptance
- [ ] Neighbor lookup is O(1) or O(log n)
- [ ] No function signature changes to `npc_kg_add_edge`
- [ ] Existing A* tests unaffected
