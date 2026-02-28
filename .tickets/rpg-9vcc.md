---
id: rpg-9vcc
status: open
deps: [rpg-caw8]
links: []
created: 2026-02-28T22:19:57Z
type: epic
priority: 1
assignee: KMD
tags: [editor, mesh, modeling]
---
# Phase 2: Core Modeling Operations

Core topological editing operations for the mesh modeling mode. Implements extrude, inset, outset, bevel, bridge, connect, merge, collapse, subdivide, detach, split, normal operations, triangulate, and quadrangulate.

READ FIRST: ref/mesh_modeling_spec.md §Topological Editing Commands

These are the primary modeling tools that transform mesh topology. All operations work on the current mesh element selection and modify the server-side mesh_slot_t geometry buffers. Each operation must maintain mesh integrity (no degenerate triangles, consistent winding, valid indices).

Key considerations:
- All operations modify indexed triangle mesh data in-place
- Must handle edge cases: empty selection, single element, entire mesh
- Extrude is the most critical operation — it creates new faces by duplicating and offsetting
- Subdivide must support multiple schemes (Catmull-Clark for smooth, linear for simple)
- Bridge requires matching edge loops between disconnected selections

