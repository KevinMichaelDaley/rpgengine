---
id: rpg-nav06
status: closed
deps: [rpg-nav03]
links: [rpg-nav04, rpg-nav05, rpg-nav03]
created: 2026-05-03T20:00:00Z
type: bug
priority: 1
assignee: KMD
parent: rpg-nav01
tags: [navigation, bug, graph, edges, connectivity]
---
# Nav Graph Has Zero Connectivity — Edges Never Created

`npc_nav_graph_extract` in `src/npc/nav/npc_nav_graph_build.c:226-296` creates isolated nodes from connected walkable components but never calls `npc_nav_graph_add_edge`. The resulting graph has zero connectivity — graph-based and hierarchical pathfinding are completely non-functional.

## Root Cause
The BFS flood per component creates a node for each region but:
1. Never records adjacency between adjacent walkable components
2. Never creates portal edges at section boundaries
3. The `npc_nav_hgraph_reduce` similarly creates hierarchical clusters without inter/intra-level edges

## Fix
1. In `npc_nav_graph_extract`: after finding connected components, scan boundary voxels to detect adjacent regions and add edges
2. In `npc_nav_hgraph_reduce`: add edges between clusters that share crossing edges from the level below

## Acceptance
- [ ] Graph extraction produces edges between adjacent walkable regions
- [ ] Graph A* (`npc_graph_astar`) finds paths across connected regions
- [ ] Hierarchical reduction preserves connectivity at all levels
