---
id: rpg-zpwy
status: closed
deps: [rpg-clq6]
links: []
created: 2026-02-28T22:24:09Z
type: task
priority: 2
assignee: KMD
parent: rpg-9vcc
tags: [editor, mesh, topology]
---
# Detach, split, flip/recalculate normals

Implement detach, split, and normal operations.

detach: Move selected faces to a new mesh slot (@scratch), optionally keeping the original.
- Removes selected faces from active mesh
- Creates new mesh in target slot with only the detached geometry
- Compacts vertex buffer (removes unreferenced vertices)

split: Split shared vertices at selection boundary. Vertices shared between selected and unselected faces get duplicated so selected faces have independent copies. This breaks smooth shading at the boundary.

flip_normals: Reverse face winding (swap index order within each selected triangle). This inverts the surface normal direction.

recalculate_normals: Regenerate normals for selected faces.
- 'flat': each face gets its geometric normal (no sharing)
- 'smooth': average normals of adjacent faces per vertex
- 'weighted': weight by face area when averaging

Args:
- detach: {"keep_original": bool}
- flip_normals: {} (operates on selection)
- recalculate_normals: {"method": "flat"|"smooth"|"weighted"}

Files to create:
- src/editor/mesh/mesh_detach.c — detach + compact
- src/editor/mesh/mesh_split.c — vertex splitting at boundaries
- src/editor/mesh/mesh_normals.c — flip and recalculate normals
- src/editor/commands/cmd_mesh_normals.c — command handlers
- tests/editor/mesh_normals_tests.c

## Acceptance Criteria

- Detach removes selected faces from active mesh
- Detach creates valid mesh in target slot
- Detach with keep_original=true leaves original intact
- Split duplicates shared boundary vertices
- flip_normals reverses triangle winding order
- Flat normals: each face vertex gets the face geometric normal
- Smooth normals: vertex normals are averaged from adjacent faces
- Weighted: larger faces contribute more to vertex normal
- Tests: detach, detach keep, split boundary, flip, flat, smooth, weighted

