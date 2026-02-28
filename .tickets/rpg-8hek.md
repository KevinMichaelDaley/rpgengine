---
id: rpg-8hek
status: open
deps: [rpg-clq6]
links: []
created: 2026-02-28T22:23:53Z
type: task
priority: 2
assignee: KMD
parent: rpg-9vcc
tags: [editor, mesh, topology]
---
# Subdivide (Catmull-Clark, loop, linear)

Implement mesh subdivision with three schemes.

linear: Simple midpoint subdivision. Each triangle is split into 4 by inserting midpoints on each edge. No smoothing — geometry stays flat.

Catmull-Clark: Smoothing subdivision (the standard for organic shapes). Each face gets a face point (centroid), each edge gets an edge point (average of edge midpoint and adjacent face centroids), original vertices are repositioned based on neighboring face/edge points.

loop: Triangle-specific subdivision scheme. Better for all-triangle meshes than Catmull-Clark. Uses weights based on vertex valence.

All schemes support 'levels' parameter for recursive application.

Args:
- subdivide: {"method": "linear"|"catmull-clark"|"loop", "levels": int}

Files to create:
- src/editor/mesh/mesh_subdiv_linear.c — linear subdivision
- src/editor/mesh/mesh_subdiv_catmull.c — Catmull-Clark subdivision
- src/editor/mesh/mesh_subdiv_loop.c — Loop subdivision
- src/editor/commands/cmd_subdivide.c — command handler
- tests/editor/mesh_subdivide_tests.c

## Acceptance Criteria

- Linear: triangle count quadruples per level
- Linear: vertex positions are exact midpoints (no smoothing)
- Catmull-Clark level 1: smooth surface, vertex count = V + E + F
- Loop level 1: triangle count quadruples, smooth surface
- Multi-level: 2 levels of linear on single triangle produces 16 triangles
- UVs correctly interpolated during subdivision
- Normals recalculated after subdivision
- Tests: linear 1 level, linear 2 levels, catmull-clark 1 level, loop 1 level, UV preservation

