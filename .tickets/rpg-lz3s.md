---
id: rpg-lz3s
status: closed
deps: [rpg-nulp, rpg-m6jf]
links: []
created: 2026-02-28T22:22:21Z
type: task
priority: 2
assignee: KMD
parent: rpg-caw8
tags: [editor, mesh, client, rendering]
---
# Client mesh rendering (basic wireframe + solid)

Implement client-side rendering of editable meshes received from the server.

The client receives FVMA binary blobs and must:
1. Deserialize into GPU-uploadable vertex/index buffers
2. Render solid shaded mesh (positions + normals → basic lighting)
3. Render wireframe overlay (all edges, depth-tested)
4. Render selection highlights based on current mode:
   - Selected vertices: colored billboards (8px)
   - Selected edges: colored lines (4px) with depth bias
   - Selected faces: tinted overlay (multiply blend) + 2px highlight border
   - Active polygroup: subtle tint different from selection

This is client-side OpenGL rendering code and requires the GL context. Tests will validate the data upload path but not actual rendering (headless).

Files to create:
- include/ferrum/editor/client/client_mesh_render.h — render state type, API
- src/editor/client/client_mesh_render.c — VAO upload, draw calls
- src/editor/client/client_mesh_highlight.c — selection highlight rendering
- tests/editor/client_mesh_render_tests.c (data path only, no GL)

## Acceptance Criteria

- FVMA blob deserialized into vertex/index arrays ready for GPU upload
- Wireframe edge list correctly extracted from triangle indices
- Selection highlight data correctly generated for each selection mode
- Face tint overlay indices match selected face bitset
- Edge highlight indices match selected edge bitset
- Vertex billboard positions match selected vertex positions
- Tests: data deserialization, edge extraction, highlight generation
- Note: actual GL rendering tested manually, not in automated tests

