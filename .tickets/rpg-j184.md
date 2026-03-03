---
id: rpg-j184
status: open
deps: []
links: []
created: 2026-03-02T18:36:54Z
type: task
priority: 1
assignee: KMD
---
# Phase 1a: static_mesh_t type and VAO loading

Create the static_mesh_t type system for immutable renderable geometry. See ref/renderer_spec.md §2.1 for full specification.

Deliverables:
- include/ferrum/renderer/mesh/static_mesh.h: static_mesh_t struct (VAO + per-attribute VBOs + IBO + submeshes + bounds), render_submesh_t struct, create/destroy/draw API
- src/renderer/mesh/static_mesh_create.c: Allocate GPU resources, bind VAO attributes at canonical locations (pos=0, normal=1, tangent=2, uv0=3, uv1=4, color=5)
- src/renderer/mesh/static_mesh_destroy.c: Release all GPU resources
- src/renderer/mesh/static_mesh_draw.c: Bind VAO, issue glDrawElements per submesh
- src/renderer/mesh/static_mesh_fvma.c: static_mesh_create_from_fvma() — deserialize existing FVMA binary format into the new struct. Replaces build_vao_from_fvma() in demo_client
- src/renderer/mesh/static_mesh_primitives.c: static_mesh_create_box/sphere/capsule/plane — replaces gen_*_mesh() in demo_client
- Missing optional attributes bind 1-element default VBOs so shaders dont need permutations for attribute presence
- Bounding sphere and AABB computed at creation time for frustum culling
- Tests in tests/p004_renderer_static_mesh_tests.c

Constraints: ≤2 public types per header, ≤4 non-static functions per source file. VAO attribute locations match ref/renderer_spec.md §8.

