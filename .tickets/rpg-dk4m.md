---
id: rpg-dk4m
status: in_progress
deps: [rpg-clq6]
links: []
created: 2026-02-28T22:23:12Z
type: task
priority: 2
assignee: KMD
parent: rpg-9vcc
tags: [editor, mesh, topology]
---
# Bevel (edges and vertices)

Implement edge and vertex bevel (chamfer) operations.

Edge bevel: Replace each selected edge with a strip of faces, rounding the corner. The 'amount' controls how far the new edge is from the original, and 'segments' controls subdivision of the bevel profile.
Algorithm:
1. For each selected edge, split adjacent faces at the bevel distance
2. Create new faces filling the beveled region
3. With segments > 1, add intermediate vertex rings for rounded profile
4. Profile shape: 'linear' (flat chamfer), 'convex' (outward curve), 'concave' (inward curve)

Vertex bevel: Replace each selected vertex with a small polygon (one vertex per incident edge).

Args:
- bevel: {"amount": float, "segments": int, "profile": "linear"|"convex"|"concave"}

Files to create:
- src/editor/mesh/mesh_bevel.c — edge bevel core algorithm
- src/editor/mesh/mesh_bevel_vertex.c — vertex bevel variant
- src/editor/commands/cmd_bevel.c — command handler
- tests/editor/mesh_bevel_tests.c

## Acceptance Criteria

- Edge bevel on box edge: replaces sharp edge with chamfer face(s)
- segments=1: flat chamfer (single quad strip)
- segments=3: rounded bevel with 3 intermediate rings
- Profile shapes produce correct vertex positions
- Vertex bevel replaces vertex with polygon
- Adjacent face topology remains valid
- Tests: single edge, multi edge, segments, profiles, vertex bevel

