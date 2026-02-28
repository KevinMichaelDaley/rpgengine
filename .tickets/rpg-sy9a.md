---
id: rpg-sy9a
status: open
deps: [rpg-ivbg, rpg-k49h]
links: []
created: 2026-02-28T22:24:59Z
type: task
priority: 2
assignee: KMD
parent: rpg-6fi0
tags: [editor, mesh, uv]
---
# Smart unwrap (angle-based flattening)

Implement automatic UV unwrapping using angle-based flattening (ABF).

Smart unwrap splits the mesh into UV islands at sharp edges (angle > threshold), then flattens each island to minimize UV distortion. This is the 'just make it work' unwrap method.

Algorithm:
1. Detect sharp edges based on angle_threshold between adjacent face normals
2. Split mesh into UV islands at sharp edges and marked seams
3. For each island, flatten to 2D using conformal mapping or ABF
4. Apply stretch_weight to balance between angle preservation and area preservation

Args:
- unwrap_smart: {"angle_threshold": float, "stretch_weight": float}

Files to create:
- src/editor/mesh/mesh_uv_smart.c — island detection + ABF flattening
- src/editor/mesh/mesh_uv_island.c — UV island extraction and boundary detection
- tests/editor/mesh_uv_smart_tests.c

## Acceptance Criteria

- Correctly splits mesh into islands at sharp edges
- Each island produces non-overlapping UVs
- Angle threshold controls island splitting granularity
- Low stretch_weight: preserves angles (conformal); high: preserves areas
- Simple box: produces 6 islands (one per face group)
- Tests: box islands, cylinder islands, angle threshold sensitivity, stretch weight

