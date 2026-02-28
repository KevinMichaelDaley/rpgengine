---
id: rpg-i72w
status: closed
deps: [rpg-clq6]
links: []
created: 2026-02-28T22:24:25Z
type: task
priority: 2
assignee: KMD
parent: rpg-9vcc
tags: [editor, mesh, topology]
---
# Triangulate and quadrangulate

Implement triangulation and quadrangulation operations.

triangulate: Convert any n-gon faces to triangles. Since our mesh format is already triangle-based, this primarily applies after operations that conceptually produce quads (like bevel or bridge) — it ensures the mesh remains pure triangles.
Methods:
- 'ear_clip': ear clipping algorithm (handles concave polygons)
- 'delaunay': constrained Delaunay triangulation (better triangle quality)

quadrangulate: Merge adjacent triangle pairs into logical quads where appropriate. Uses angle threshold to find triangle pairs that form good quads (low deviation from planar). This is a display/export hint — internal storage remains triangles, but quad metadata is stored for export.

Args:
- triangulate: {"method": "ear_clip"|"delaunay"}
- quadrangulate: {"angle_threshold": float}

Files to create:
- src/editor/mesh/mesh_triangulate.c — ear clip and Delaunay triangulation
- src/editor/mesh/mesh_quadrangulate.c — triangle-pair to quad detection
- src/editor/commands/cmd_triangulate.c — command handlers
- tests/editor/mesh_triangulate_tests.c

## Acceptance Criteria

- Ear clip correctly triangulates convex and concave polygons
- Delaunay produces no sliver triangles for well-formed input
- Quadrangulate identifies triangle pairs forming near-planar quads
- angle_threshold filters out poor quad candidates
- Already-triangulated mesh: triangulate is a no-op
- Tests: convex polygon, concave polygon, ear clip, delaunay, quad detection, threshold

