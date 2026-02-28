---
id: rpg-9bds
status: closed
deps: [rpg-clq6]
links: []
created: 2026-02-28T22:25:49Z
type: task
priority: 2
assignee: KMD
parent: rpg-6fi0
tags: [editor, mesh, materials]
---
# Per-face material assignment

Implement per-face material assignment for meshes. Unlike entity-level material slots, mesh faces can have different materials via polygroup-to-material mapping.

material_assign: Apply a material path to selected faces. All selected faces get assigned to a polygroup that maps to the specified material. If a matching polygroup already exists, faces join it; otherwise a new polygroup ID is created.

The polygroup_id per face (u16) serves double duty: it groups faces for selection AND maps to a material path via a polygroup→material table in mesh_slot_t.

Args:
- material_assign: {"material_path": string, "face_indices": [...]}

Files to create:
- include/ferrum/editor/mesh/mesh_material.h — polygroup-material mapping type
- src/editor/mesh/mesh_material.c — assign, get mapping table, polygroup management
- src/editor/commands/cmd_mesh_material.c — command handler
- tests/editor/mesh_material_tests.c

## Acceptance Criteria

- Assigning material to faces sets their polygroup to the material's group
- Multiple materials: different face groups get different polygroup IDs
- Polygroup→material mapping correctly maintained
- Re-assigning same material reuses existing polygroup ID
- Get material for face returns the assigned material path
- Tests: assign single, assign multiple materials, reassign, get mapping

