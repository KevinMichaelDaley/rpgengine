---
id: rpg-nav04
status: open
deps: [rpg-nav03]
links: [rpg-nav01, rpg-nav05]
created: 2026-04-27T00:25:00Z
type: task
priority: 1
assignee: KMD
parent: rpg-nav01
tags: [navigation, astar, pathfinding, hierarchical, graph]
---
# Modular Hierarchical A* Pathfinder

A generic hierarchical A* implementation that operates on any level of the navigation graph abstraction, falling back to SVO A* at the bottom level.

## Requirements

### 1. Hierarchical A* Algorithm

```c
typedef struct npc_hpath_request {
    vec3_t   start_pos;
    vec3_t   goal_pos;
    uint32_t start_section;
    uint32_t goal_section;
    float    agent_radius;
    float    agent_height;
} npc_hpath_request_t;

typedef struct npc_hpath_result {
    uint32_t waypoint_count;
    vec3_t  *waypoints;       /* arena-allocated */
    float    total_cost;
    bool     partial;         /* true if only partial path found */
} npc_hpath_result_t;
```

Algorithm:
1. Find nearest graph node at each level for start and goal.
2. **Abstract search**: run A* on the highest level containing both start and goal. This gives a sequence of high-level clusters.
3. **Refinement**: for each adjacent pair of clusters in the abstract path, run A* on the next lower level within the union of those two clusters.
4. **Base level**: when refinement reaches L1 (chunk graph), run A* on the chunk graph within the relevant sections.
5. **Ground level**: for the final leg within a single chunk, run A* directly on the SVO voxels.

### 2. SVO A* (Ground Level)

```c
bool npc_svo_astar(const npc_svo_grid_t *svo,
                   const npc_nav_blocker_t *blockers,
                   uint32_t blocker_count,
                   vec3_t start,
                   vec3_t goal,
                   vec3_t *out_waypoints,
                   uint32_t *out_count,
                   uint32_t max_waypoints);
```

- Operates on voxel coordinates (not world space).
- Heuristic: Euclidean distance × voxel_size.
- Dynamic blockers checked at expansion time (AABB intersects voxel → blocked).
- Returns waypoints at voxel centers; post-process with line-of-sight shortcutting.

### 3. Graph A* (L1+)

```c
bool npc_graph_astar(const npc_nav_graph_t *graph,
                     uint32_t start_node,
                     uint32_t goal_node,
                     uint32_t *out_nodes,
                     uint32_t *out_count,
                     uint32_t max_nodes);
```

- Standard A* on weighted graph.
- Heuristic: Euclidean distance between node centroids.
- Edge cost = precomputed graph edge cost.

### 4. Query-Time Dynamic Blockers

- Dynamic obstacles are NOT baked into the graph.
- During graph A*, if an edge's bounding corridor intersects a dynamic blocker, the edge is temporarily invalid for this query.
- During SVO A*, check blocker AABB against each expanded voxel.
- This allows doors to open/close and temporary walls to appear without rebuilding the graph.

### 5. Modular Level Selection

```c
typedef enum {
    NPC_PATH_SVO_ONLY,       /* brute-force SVO A* */
    NPC_PATH_CHUNK_GRAPH,    /* L1 chunk graph + SVO fallback */
    NPC_PATH_HIERARCHICAL,   /* full L2+ hierarchical */
    NPC_PATH_NAVMESH,        /* navmesh layer (deferred) */
} npc_path_strategy_t;
```

- The pathfinder accepts a strategy enum.
- `NPC_PATH_NAVMESH` is a stub that falls back to `NPC_PATH_HIERARCHICAL` until navmeshes exist.
- This modularity allows benchmarking and gradual rollout.

## Files to Create

- `include/ferrum/npc/npc_pathfind.h` — path request/result types, strategy enum
- `src/npc/nav/npc_pathfind_hastar.c` — hierarchical A* orchestration
- `src/npc/nav/npc_pathfind_graph_astar.c` — graph A* implementation
- `src/npc/nav/npc_pathfind_svo_astar.c` — SVO voxel A* implementation
- `src/npc/nav/npc_pathfind_shortcut.c` — line-of-sight waypoint reduction
- `tests/npc/npc_pathfind_tests.c` — correctness + performance tests

## Acceptance

- [ ] SVO A* finds shortest path in empty 10m corridor.
- [ ] SVO A* routes around a box obstacle.
- [ ] Graph A* finds path through 5-room building.
- [ ] Hierarchical A* is > 10× faster than SVO-only for 100m+ distances.
- [ ] Dynamic blocker blocks a doorway and forces alternate route.
- [ ] Partial path returned when goal is unreachable (e.g., behind destroyed bridge).
- [ ] Line-of-sight shortcutting reduces waypoint count by > 50%.
