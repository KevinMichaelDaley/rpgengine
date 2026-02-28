---
id: rpg-1tji
status: closed
deps: [rpg-elky, rpg-9bds]
links: []
created: 2026-02-28T22:27:10Z
type: task
priority: 2
assignee: KMD
parent: rpg-37uq
tags: [editor, mesh, integration]
---
# mesh_commit (bake editable mesh to world entity)

Implement mesh_commit — the command that converts an editable mesh in a mesh_slot_t into a permanent world entity with static mesh geometry.

mesh_commit:
1. Serialize the active mesh slot to FVMA binary format
2. Store the binary blob in the asset registry with a generated path
3. Create a new entity in the entity store with the mesh as its geometry
4. Optionally apply a material override to all faces
5. Position the entity at the mesh's bounding box center (or cursor position)
6. Clear the mesh slot after commit (or keep if specified)

The committed entity becomes a regular editor entity — selectable, movable, clonable, saveable. The mesh data is stored as an asset.

Args:
- mesh_commit: {"entity_name": string, "material_override": string}

Files to create:
- src/editor/mesh/mesh_commit.c — bake mesh to entity + asset
- src/editor/commands/cmd_mesh_commit.c — command handler
- tests/editor/mesh_commit_tests.c

## Acceptance Criteria

- Committed mesh creates a new entity in the store
- Entity has correct position (bounding box center or cursor)
- Asset registered with generated path (@committed/entity_name.fvma)
- Material override applied to entity material slots if specified
- Mesh slot cleared after commit (by default)
- Committed entity selectable and manipulable
- Tests: basic commit, with name, with material, entity position, slot cleared

