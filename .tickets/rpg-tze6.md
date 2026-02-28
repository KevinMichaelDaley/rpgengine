---
id: rpg-tze6
status: closed
deps: [rpg-ma1t]
links: []
created: 2026-02-28T22:21:34Z
type: task
priority: 2
assignee: KMD
parent: rpg-caw8
tags: [editor, mesh, selection]
---
# Selection mode system (vertex/edge/face/polygroup/object)

Implement the selection mode system for mesh editing. The 'mode' command switches between five selection topologies that determine what mesh elements are targeted by selection and editing commands.

Modes:
- vertex: individual vertex indices
- edge: edge indices (pairs of vertex indices, derived from triangle adjacency)
- face: triangle indices (index_count / 3)
- polygroup: face group IDs (u16 per face)
- object: entire mesh (no sub-element selection)

The mode command on the server updates mesh_edit_t.current_mode. When mode changes, the current selection is converted: e.g., switching from face→vertex selects all vertices of selected faces; vertex→face selects faces where ALL vertices are selected.

Edge data structure: edges are implicit from the index buffer. Build an edge table (sorted vertex pair → edge index) for O(1) lookup. Edge count = unique pairs across all triangles.

Files to create:
- include/ferrum/editor/mesh/mesh_selection.h — selection mode enum, conversion API, edge table
- src/editor/mesh/mesh_selection.c — mode set, selection convert, edge table build
- src/editor/mesh/mesh_edge_table.c — edge extraction from index buffer, adjacency
- src/editor/commands/cmd_mesh_mode.c — 'mode' command handler
- tests/editor/mesh_selection_tests.c

## Acceptance Criteria

- 5 modes correctly stored and queryable
- Edge table correctly extracts unique edges from triangle indices
- face→vertex conversion: selects all vertices of selected faces
- vertex→face conversion: selects faces where all 3 vertices are selected
- edge→face: selects faces containing selected edges
- face→edge: selects all edges of selected faces
- Mode command registered as 'mode' with args {"type":"vertex|edge|face|polygroup|object"}
- Selection state persists across mode switches (with conversion)
- Tests: mode switching, all 4 conversion paths, edge table extraction, empty selection

