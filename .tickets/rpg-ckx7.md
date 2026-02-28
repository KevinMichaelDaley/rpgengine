---
id: rpg-ckx7
status: open
deps: [rpg-fund, rpg-clq6]
links: []
created: 2026-02-28T22:27:24Z
type: task
priority: 2
assignee: KMD
parent: rpg-37uq
tags: [editor, mesh, undo]
---
# Undo/redo integration for all mesh operations

Integrate full undo/redo support for all mesh modeling operations.

Every mesh-modifying command must push an undo entry before making changes. For mesh operations, the undo snapshot is the entire mesh_slot_t state (or a delta if the mesh is large).

Strategies:
- Small meshes (<64K vertices): full slot snapshot in undo arena
- Large meshes: delta compression (only store changed vertices/indices)
- Selection changes: snapshot selection bitsets only (much smaller)

The undo system must handle:
- Topology changes (extrude, subdivide, etc.) — full snapshot required
- Vertex moves (transform) — position delta only
- Selection changes — bitset delta
- Mode changes — mode enum only

Files to create:
- src/editor/mesh/mesh_undo.c — mesh-specific undo snapshot/restore
- src/editor/mesh/mesh_undo_delta.c — delta compression for large meshes
- tests/editor/mesh_undo_tests.c

## Acceptance Criteria

- Every mesh command is undoable
- Undo restores exact previous mesh state
- Redo re-applies the undone operation
- Small mesh undo: full snapshot fits in arena
- Large mesh undo: delta compression reduces memory usage
- Selection undo: restores previous selection state
- Undo across mode switches: restores previous mode
- Tests: undo extrude, undo move, undo select, redo, undo chain, delta compression

