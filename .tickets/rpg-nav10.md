---
id: rpg-nav10
status: closed
deps: [rpg-nav03]
links: [rpg-nav03]
created: 2026-05-03T20:00:00Z
type: bug
priority: 2
assignee: KMD
parent: rpg-nav01
tags: [navigation, bug, clustering, reduction, z-height]
---
# 2D-Only Clustering in Nav Graph Reduction Ignores Z Height

`simple_cluster` in `src/npc/nav/npc_nav_graph_reduce.c:60-73` partitions nodes by X and Y only:
```c
/* 2D grid division (X,Y). */
```
Nodes at very different heights with the same X,Y coordinates are clustered together. This is incorrect for navigation — an NPC cannot walk from the ground floor to the third floor simply because the nodes share X,Y.

## Fix
Extend the clustering to 3D grid division (X,Y,Z). Adjust the bucket count formula:
```c
uint32_t gz = (uint32_t)ceilf(cbrtf((float)target_clusters));
uint32_t gx = gz, gy = gz;
```
Or use a KD-tree split along the axis with largest variance.

## Acceptance
- [ ] Nodes at floor 1 and floor 5 with same X,Y are NOT in the same cluster
- [ ] Multi-story building produces separate clusters per floor
- [ ] Existing single-floor tests unaffected
