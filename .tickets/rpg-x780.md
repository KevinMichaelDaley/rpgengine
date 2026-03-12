---
id: rpg-x780
status: open
deps: []
links: []
created: 2026-03-12T06:48:52Z
type: task
priority: 2
assignee: KMD
parent: rpg-c55w
---
# §3 Engine: Collision Mesh Asset Storage

See ref/scene_editor_design.md Engine-Side Work table. Collision mesh asset storage in src/asset/collision_mesh_asset.c — per-entity collision mesh separate from render mesh.

## Acceptance Criteria

Engine stores collision meshes separately from render meshes per entity. Cross-reference with ref/scene_editor_spec.md and ref/scene_editor_ux.md for requirements and interaction details.

