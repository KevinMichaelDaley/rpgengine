---
id: rpg-29ky
status: in_progress
deps: [rpg-clq6]
links: []
created: 2026-02-28T22:23:24Z
type: task
priority: 2
assignee: KMD
parent: rpg-9vcc
tags: [editor, mesh, topology]
---
# Bridge and connect

Implement bridge and connect topology operations.

bridge: Create faces connecting two separate edge loops or face selections. Used to join disconnected mesh regions (e.g., two holes in a surface).
Algorithm:
1. Identify two boundary edge loops from the selection
2. Match vertices between loops (by proximity or index order)
3. Create quad faces connecting corresponding edges

connect: Insert a loop cut between two selected edges, subdividing adjacent faces. This adds an edge loop across the mesh surface.
Algorithm:
1. Find a path of faces connecting the selected edges
2. Insert new vertices along each crossed face edge
3. Create new edges connecting the inserted vertices

Args:
- bridge: {"edge_indices": [...]} or {"face_indices": [...]}
- connect: {"edge_indices": [...]}

Files to create:
- src/editor/mesh/mesh_bridge.c — bridge between edge loops
- src/editor/mesh/mesh_connect.c — loop cut insertion
- src/editor/commands/cmd_bridge.c — command handlers
- tests/editor/mesh_bridge_tests.c

## Acceptance Criteria

- Bridge two parallel edge loops: creates quad strip between them
- Bridge handles different vertex counts (interpolation)
- Connect inserts edge loop across faces between two selected edges
- New faces have correct winding and normals
- Tests: parallel loops, mismatched counts, connect single cut, connect multi

