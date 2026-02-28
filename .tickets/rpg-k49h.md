---
id: rpg-k49h
status: open
deps: [rpg-ivbg]
links: []
created: 2026-02-28T22:25:11Z
type: task
priority: 2
assignee: KMD
parent: rpg-6fi0
tags: [editor, mesh, uv]
---
# UV seam marking and clearing

Implement UV seam marking on edges. Seam edges tell the unwrapper where to cut the mesh into UV islands.

seam_mark: Mark selected edges as UV seams. Seam edges split UV coordinates — vertices on a seam edge have different UV values on each side.

seam_clear: Remove seam marking from selected edges.

Seam state is stored as a bitset in mesh_slot_t (one bit per edge). When seams change, UV islands may need to be recalculated.

Args:
- seam_mark: {"edge_indices": [...]}
- seam_clear: {"edge_indices": [...]}

Files to create:
- src/editor/mesh/mesh_uv_seam.c — seam mark/clear, seam edge bitset management
- src/editor/commands/cmd_seam.c — command handlers
- tests/editor/mesh_uv_seam_tests.c

## Acceptance Criteria

- seam_mark sets seam flag on specified edges
- seam_clear removes seam flag
- Seam edges visible in selection highlighting (different color)
- Seam state persists across mode switches
- Smart unwrap respects marked seams
- Tests: mark, clear, toggle, persistence, unwrap with seams

