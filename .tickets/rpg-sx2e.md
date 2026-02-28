---
id: rpg-sx2e
status: closed
deps: [rpg-8hi9]
links: []
created: 2026-02-28T22:26:37Z
type: task
priority: 2
assignee: KMD
parent: rpg-37uq
tags: [editor, mesh, csg]
---
# CSG operations (hollow, merge, subtract, intersect)

Implement constructive solid geometry (CSG) boolean operations on meshes.

csg_hollow: Hollow out a solid mesh by creating an inner shell offset inward by 'thickness'. Creates a room/container from a solid block.

csg_merge: Boolean union of two meshes. Combines geometry, removing internal faces.

csg_subtract: Boolean difference. Removes the volume of the cutter mesh from the target. Creates holes, doorways, windows.

csg_intersect: Boolean intersection. Keeps only the volume where both meshes overlap.

CSG operations are computationally complex and require robust mesh-mesh intersection. Use a BSP-tree or plane-sweep approach for robustness.

Args:
- csg_hollow: {"thickness": float}
- csg_merge: {"target_entities": [...]}
- csg_subtract: {"cutter_entity": int}
- csg_intersect: {"target_entity": int}

Files to create:
- src/editor/mesh/mesh_csg_hollow.c — shell generation
- src/editor/mesh/mesh_csg_boolean.c — union/subtract/intersect core
- src/editor/mesh/mesh_csg_classify.c — vertex/edge/face classification against other mesh
- src/editor/commands/cmd_csg.c — command handlers
- tests/editor/mesh_csg_tests.c

## Acceptance Criteria

- Hollow creates inner shell with inverted normals
- Hollow thickness parameter controls wall width
- CSG merge of two overlapping boxes: internal faces removed
- CSG subtract of box from box: creates rectangular hole
- CSG intersect of two overlapping boxes: produces overlap volume
- All operations produce watertight meshes (no holes)
- Tests: hollow box, merge two boxes, subtract centered, intersect offset, edge cases

