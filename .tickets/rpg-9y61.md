---
id: rpg-9y61
status: open
deps: [rpg-07mf]
links: [rpg-d3ue, rpg-h66f, rpg-zryp, rpg-hezs, rpg-8ot1]
created: 2026-03-04T02:55:34Z
type: task
priority: 2
assignee: KMD
---
# Phase 1 visual test: mesh primitives and registry

End-to-end graphical test for Phase 1 renderer deliverables. Creates an SDL2+GL window, renders all primitive mesh types (box, sphere, capsule, plane) plus a custom FVMA mesh using static_mesh_t and skeletal_mesh_t, records 3 seconds of video via fr_video_capture_t.

Test verifies:
- All primitive generators produce visible geometry
- Per-attribute VBOs bind correctly at canonical locations (pos=0, normal=1, tangent=2, uv0=3, uv1=4, color=5)
- Skeletal mesh renders with bone-weighted vertices
- mesh_registry_t handles are valid across create/lookup/destroy cycles
- FVMA round-trip: serialize a mesh_slot_t, load via static_mesh_create_from_fvma, render
- Missing optional attributes (tangents, UV1, colors) use default VBOs without visual artifacts
- Submesh drawing: multi-material mesh draws each submesh separately
- Bounding sphere/AABB overlay (debug lines) matches geometry extents

Scene layout: 5 objects in a row (box, sphere, capsule, plane, custom mesh) with a slow orbit camera. Basic MVP shader with per-object flat color. Output: tests/output/phase1_mesh_primitives.mp4

File: tests/visual/p004_visual_mesh_primitives.c
Build rule: add to Makefile, link SRC_ALL + OBJ_GLAD + RENDERER_TEST_LIBS
Duration: 3 seconds at 30fps (90 frames)
Exit: PASS if frame count >= 90 and no GL errors, FAIL otherwise.

