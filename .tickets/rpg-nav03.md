---
id: rpg-nav03
status: open
deps: [rpg-nav02]
links: [rpg-nav01, rpg-nav04, rpg-llm04]
created: 2026-04-27T00:25:00Z
type: task
priority: 1
assignee: KMD
parent: rpg-nav01
tags: [navigation, graph, abstraction, portal, svo, reduction]
---
# Graph Construction and Hierarchical Reduction from SVO

Extract a navigability graph from the SVO, then build hierarchical cluster graphs for fast long-distance pathfinding.

## Requirements

### 1. Chunk Graph (L1 — one graph per SVO section)

Each section becomes a graph where:
- **Nodes** = contiguous walkable regions (flood-fill connected components within the section).
- **Edges** = adjacency between regions, plus portals to neighboring sections.

```c
typedef struct npc_nav_graph_node {
    uint32_t node_id;
    uint32_t section_id;
    phys_aabb_t bounds;       /* bounding box of region */
    vec3_t   centroid;        /* center of mass of walkable voxels */
    float    radius;          /* approximate radius from centroid */
} npc_nav_graph_node_t;

typedef struct npc_nav_graph_edge {
    uint32_t to_node_id;
    float    cost;            /* Euclidean distance between centroids */
    uint32_t flags;           /* PORTAL, STAIRS, LADDER, etc. */
} npc_nav_graph_edge_t;
```

Construction:
1. For each section, find all connected walkable components (6-connectivity).
2. Each component = one node.
3. If two components touch the same section boundary face, create a portal edge to the adjacent section's matching component.

### 2. Hierarchical Reduction (L2+)

Cluster nearby chunk-graph nodes into higher-level nodes using recursive graph partitioning:

1. **Level 1** = chunk graph (raw SVO regions).
2. **Level 2** = cluster ~4-8 nearby L1 nodes into a super-node. Edges = sum of crossing L1 edges.
3. **Level 3+** = repeat until the top level has < 64 nodes.

```c
typedef struct npc_nav_hnode {
    uint32_t level;
    uint32_t child_start;     /* index into child array */
    uint32_t child_count;
    phys_aabb_t bounds;
    vec3_t   centroid;
} npc_nav_hnode_t;

typedef struct npc_nav_hgraph {
    npc_nav_hnode_t *nodes_per_level[8];
    uint32_t         node_count_per_level[8];
    uint32_t         level_count;
} npc_nav_hgraph_t;
```

- Partitioning heuristic: minimize edge cut, keep clusters roughly spherical.
- Portal preservation: any edge crossing a section boundary must remain visible at all levels (don't collapse across portals).

### 3. Dynamic Updates

- When a section's SVO is rebuilt, rebuild that section's chunk graph (L1).
- Update L2+ clusters incrementally: mark affected clusters dirty, re-partition only those clusters.
- Performance target: L1 rebuild < 2 ms; L2 update < 5 ms.

### 4. Audio Propagation Reuse

The same chunk graph (L1) is used by the audio propagation system (rpg-llm04):
- Acoustic beam tracing seeds are placed at graph node centroids.
- Graph edges define initial acoustic coupling candidates.
- This eliminates the need for a separate room graph.

## Files to Create

- `include/ferrum/npc/npc_nav_graph.h` — graph node/edge types, hierarchical graph
- `src/npc/nav/npc_nav_graph_build.c` — chunk graph extraction from SVO
- `src/npc/nav/npc_nav_graph_reduce.c` — hierarchical reduction (partitioning)
- `src/npc/nav/npc_nav_graph_update.c` — incremental update on section change
- `tests/npc/npc_nav_graph_tests.c` — build, reduction, update tests

## Acceptance

- [ ] Chunk graph has one node per walkable room in a test building.
- [ ] Portal edges connect rooms across doorways.
- [ ] Hierarchical reduction produces 3+ levels for a 100m world.
- [ ] Pathfinding on L2+ graph is > 10× faster than L1 for 50m+ queries.
- [ ] Section rebuild triggers only local L1+L2 updates.
- [ ] Audio propagation can query chunk graph for node positions.
