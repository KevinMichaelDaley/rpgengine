---
id: rpg-nav07
status: closed
deps: [rpg-nav02]
links: []
created: 2026-05-03T20:00:00Z
type: bug
priority: 1
assignee: KMD
parent: rpg-nav01
tags: [navigation, bug, svo, rasterize, mesh-aabb]
---
# mesh_aabb Misused in SVO Rasterize

`npc_svo_rasterize_triangle` in `src/npc/nav/npc_svo_rasterize.c:140-148` unions the per-triangle AABB with the `mesh_aabb` parameter (documented as "Precomputed AABB of the triangle, optional"). If a caller passes the mesh-level AABB, every triangle rasterizes the entire mesh bounding box as SOLID, making the SVO nearly 100% solid.

## Root Cause
Lines 141-148 perform:
```c
if (mesh_aabb) {
    if (mesh_aabb->min.x < tri_aabb.min.x) tri_aabb.min.x = mesh_aabb->min.x;
    ...
```
This unions rather than intersects. The parameter should expand by mesh epsilon, not replace the triangle bounds.

## Fix
- Remove the `mesh_aabb` parameter entirely since `npc_svo_rasterize_mesh` always passes NULL
- Or fix the semantics: use mesh AABB only to early-reject triangles outside grid bounds

## Acceptance
- [ ] Rasterizing a mesh does not mark voxels outside each triangle's AABB
- [ ] `npc_svo_rasterize_mesh` produces correct per-triangle rasterization
- [ ] Existing SVO tests still pass
