---
id: rpg-p2vr
status: open
deps: [rpg-j184]
links: []
created: 2026-03-02T18:37:05Z
type: task
priority: 1
assignee: KMD
---
# Phase 1b: skeletal_mesh_t type with bone weights

Create skeletal_mesh_t extending static_mesh_t with bone weight/index VBOs. See ref/renderer_spec.md §2.2.

Deliverables:
- include/ferrum/renderer/mesh/skeletal_mesh.h: skeletal_mesh_t struct (extends static_mesh_t with vbo_bone_weights, vbo_bone_indices, bone_count, inv_bind_matrices)
- src/renderer/mesh/skeletal_mesh_create.c: Allocate base mesh + bone attribute VBOs at locations 6 (vec4 weights) and 7 (ivec4 indices)
- src/renderer/mesh/skeletal_mesh_destroy.c: Free bone VBOs + inv_bind_matrices + base mesh resources
- skeletal_mesh_create_from_fvma(): Requires FVMA_FLAG_BONES in header
- Future: skeletal_mesh_create_from_gltf() stub (not implemented yet)
- Tests in tests/p004_renderer_skeletal_mesh_tests.c

Depends on: rpg-j184 (static_mesh_t must exist first)

