---
id: rpg-2nxx
status: open
deps: [rpg-07mf]
links: [rpg-tqrt, rpg-q6fa, rpg-eabp, rpg-m3nv, rpg-ebpv, rpg-hrrd]
created: 2026-03-03T03:00:02Z
type: task
priority: 2
assignee: KMD
---
# Editor integration: mesh type system (Phase 1)

Wire the new static_mesh_t and skeletal_mesh_t types into the editor entity system. See ref/renderer_spec.md Phase 1.

Deliverables:
- Add EDIT_ENTITY_TYPE_STATIC_MESH and EDIT_ENTITY_TYPE_SKELETAL_MESH to edit_entity.h
- Add mesh_handle field to edit_entity_t referencing mesh_registry entries
- Extend bridge on_spawn callback to create mesh_registry entries for new entity types (box/sphere/capsule create primitive static meshes, MESH type loads from FVMA)
- Extend bridge on_delete to release mesh_registry handles
- New editor command 'mesh_assign' to bind a mesh asset path to an entity (resolves via asset_registry, loads into mesh_registry)
- Extend 'spawn' command: when type=static_mesh or skeletal_mesh, accept a 'mesh' arg with asset path
- Extend demo_server bridge_on_spawn_ to send mesh_handle in spawn message so client can look up from its local mesh_registry
- Add SCRIPT_KEY_MESH (u32, key=15) to entity_attrs.h for mesh handle attribute access from scripts
- Tests extending editor spawn/delete tests for new entity types

Depends on: rpg-07mf (mesh_registry must exist)

