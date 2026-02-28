---
id: rpg-81td
status: open
deps: [rpg-9bds, rpg-ivbg]
links: []
created: 2026-02-28T22:26:01Z
type: task
priority: 2
assignee: KMD
parent: rpg-6fi0
tags: [editor, mesh, materials, uv]
---
# Material lift, replace, and wrap texture

Implement material sampling, replacement, and UV flow operations.

material_lift: Sample the material from a specific face (eyedropper tool). Returns the material path assigned to that face's polygroup.

material_replace: Replace all occurrences of one material with another across the entire mesh. Updates the polygroup→material mapping.

wrap_texture: Flow UV coordinates across connected faces from a source face. Starting from source_face, propagate UVs to target_faces maintaining continuity. This creates seamless texture wrapping around complex geometry.

Args:
- material_lift: {"face_index": int}
- material_replace: {"old_path": string, "new_path": string}
- wrap_texture: {"source_face": int, "target_faces": [...]}

Files to create:
- src/editor/mesh/mesh_material_lift.c — material sampling
- src/editor/mesh/mesh_material_replace.c — bulk material replacement
- src/editor/mesh/mesh_uv_wrap.c — UV flow propagation across faces
- src/editor/commands/cmd_mesh_material_ops.c — command handlers
- tests/editor/mesh_material_ops_tests.c

## Acceptance Criteria

- material_lift returns correct material path for any face
- material_replace updates all faces with old material to new material
- wrap_texture propagates UVs maintaining continuity at shared edges
- Wrap produces no UV seams at face boundaries within the target set
- Tests: lift from each polygroup, replace all, replace partial, wrap across 4 faces

