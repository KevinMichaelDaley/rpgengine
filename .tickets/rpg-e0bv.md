---
id: rpg-e0bv
status: in_progress
deps: [rpg-clq6]
links: []
created: 2026-02-28T22:23:38Z
type: task
priority: 2
assignee: KMD
parent: rpg-9vcc
tags: [editor, mesh, topology]
---
# Merge and collapse

Implement vertex merge (weld) and edge/face collapse operations.

merge: Weld selected vertices together at a target point. Modes:
- 'center': merge to centroid of selected vertices
- 'cursor': merge to current cursor position
- 'first': merge to first selected vertex
- 'last': merge to last selected vertex
- 'threshold': auto-weld vertices closer than threshold distance

After merge, update all index references to point to the surviving vertex. Remove degenerate triangles (where 2+ indices are the same vertex).

collapse: Collapse selected edges or faces to their center point.
- Edge collapse: replace edge with single vertex at midpoint, remove adjacent degenerate faces
- Face collapse: replace face with single vertex at centroid

Args:
- merge: {"target": "center"|"cursor"|"first"|"last", "threshold": float}
- collapse: {"type": "edge"|"face"}

Files to create:
- src/editor/mesh/mesh_merge.c — vertex merge/weld
- src/editor/mesh/mesh_collapse.c — edge/face collapse
- src/editor/commands/cmd_merge.c — command handlers
- tests/editor/mesh_merge_tests.c

## Acceptance Criteria

- Merge to center: centroid position is correct
- Merge updates all index references
- Degenerate triangles removed after merge
- Threshold merge: only welds vertices within distance
- Edge collapse: edge replaced by midpoint vertex
- Face collapse: face replaced by centroid vertex
- Vertex count decreases by (merged_count - 1)
- Tests: center, cursor, first, last, threshold, edge collapse, face collapse, degenerate removal

