---
id: rpg-nav02
status: open
deps: []
links: [rpg-nav01, rpg-nav03, rpg-nav04]
created: 2026-04-27T00:25:00Z
type: task
priority: 1
assignee: KMD
parent: rpg-nav01
tags: [navigation, svo, voxel, octree, spatial, static-geometry]
---
# Sparse Voxel Octree (SVO) Grid with Section-Based Chunking

Build a sparse voxel octree from static level geometry, chunked into sections for cache-friendly traversal and localized updates.

## Requirements

### 1. SVO Data Structure

```c
typedef struct npc_svo_node {
    uint32_t children[8];   /* child indices or 0xFFFFFFFF for leaf */
    uint32_t parent;        /* parent index, 0xFFFFFFFF for root */
    uint8_t  occupancy;     /* bitmask of occupied child slots */
    uint8_t  flags;         /* walkable, solid, portal, etc. */
} npc_svo_node_t;

typedef struct npc_svo_chunk {
    uint32_t section_id;    /* spatial section this chunk belongs to */
    uint32_t root_node;     /* index into global node pool */
    phys_aabb_t bounds;     /* world-space AABB of this chunk */
} npc_svo_chunk_t;

typedef struct npc_svo_grid {
    npc_svo_node_t *nodes;
    uint32_t        node_count;
    uint32_t        node_cap;
    npc_svo_chunk_t *chunks;
    uint32_t         chunk_count;
    uint32_t         chunk_cap;
    float            voxel_size; /* meters per smallest voxel */
    uint32_t         max_depth;
} npc_svo_grid_t;
```

- **Sparse**: only allocate nodes for occupied space. Empty space is implicit.
- **Max depth**: configurable (default 8 = ~0.4m voxels for 100m world).
- **Chunking**: world divided into fixed-size sections (e.g., 32m × 32m × 32m). Each section owns its SVO subtree.

### 2. Construction from Static Geometry

1. Rasterize all static mesh triangles into the SVO at max depth.
2. Mark voxels as `SOLID` if they contain geometry.
3. Flood-fill from known walkable seed points (navmesh seeds or floor voxels) to mark `WALKABLE` voxels.
4. Walkable voxels must have empty space above them (minimum agent height).

### 3. Dynamic Blockers

- Dynamic obstacles (player-built walls, doors, temporary barricades) are **not** baked into the SVO.
- Instead, they are represented as simple convex shapes (AABB, OBB, or capsule) overlaid at query time.
- A spatial hash of active dynamic blockers per section allows fast intersection tests during A* expansion.

### 4. Section-Based Updates

- When static geometry changes (e.g., wall destroyed in rpg-llm04), only the affected section is rebuilt.
- Rebuild: clear section's SVO subtree → re-rasterize triangles in section bounds → re-flood-fill.
- Performance target: single section rebuild < 10 ms on a background job.

## Files to Create

- `include/ferrum/npc/npc_svo.h` — SVO node, chunk, grid types
- `src/npc/nav/npc_svo_init.c` — grid init/destroy
- `src/npc/nav/npc_svo_rasterize.c` — triangle → voxel rasterization (≤4 non-static functions)
- `src/npc/nav/npc_svo_floodfill.c` — walkable flood-fill from seeds
- `src/npc/nav/npc_svo_blocker.c` — dynamic convex blocker overlay
- `tests/npc/npc_svo_tests.c` — construction, query, blocker overlay tests

## Acceptance

- [ ] SVO builds from a simple box mesh with correct occupied voxels.
- [ ] Walkable flood-fill marks floor voxels but not solid voxels.
- [ ] Dynamic AABB blocker marks intersected voxels as blocked during query.
- [ ] Section rebuild after geometry change updates only that section.
- [ ] Memory usage: < 1 MB per 32m³ section for typical geometry.
