---
id: rpg-07mf
status: closed
deps: [rpg-j184, rpg-p2vr]
links: []
created: 2026-03-02T18:37:19Z
type: task
priority: 1
assignee: KMD
---
# Phase 1c: mesh_registry_t with handle-based lookup

Create a central mesh registry mapping opaque handles (index+generation) to loaded mesh data. See ref/renderer_spec.md §2.3.

Deliverables:
- include/ferrum/renderer/mesh/mesh_registry.h: mesh_handle_t (index+generation), mesh_type_t enum (STATIC/SKELETAL), mesh_registry_t struct with configurable capacity (init-time allocated), freelist
- src/renderer/mesh/mesh_registry.c: mesh_registry_init(), mesh_registry_insert_static(), mesh_registry_insert_skeletal(), mesh_registry_remove(), mesh_registry_get_static(), mesh_registry_get_skeletal(), mesh_registry_is_valid()
- Shared geometry across entities via handle references
- Generation counter prevents use-after-free
- Tests in tests/p004_renderer_mesh_registry_tests.c

Depends on: rpg-j184 (static_mesh_t), rpg-p2vr (skeletal_mesh_t)

