---
id: rpg-ma1t
status: closed
deps: []
links: []
created: 2026-02-28T22:20:45Z
type: task
priority: 2
assignee: KMD
parent: rpg-caw8
tags: [editor, mesh, server]
---
# Mesh slot data structures (mesh_slot_t, mesh_edit_t)

Create the core server-side data structures for editable meshes.

mesh_slot_t holds a single editable mesh: vertex positions (vec3), normals (vec3), tangents (vec4), UVs (vec2 × 2 channels), vertex colors (vec4), triangle indices (u32), and per-face polygroup IDs (u16). All buffers are dynamically sized with capacity tracking.

mesh_edit_t is the top-level subsystem: an array of MESH_MAX_EDITABLE (16) slots, selection state (dynamic bitsets for vertices, edges, faces), active_slot index, and current selection mode enum.

Slot naming convention:
- Slot 0: @active (primary work mesh)
- Slots 1-7: @scratch (clipboard, stamps, primitives)
- Slots 8-15: @temp (preview ghosts, extrusion guides)

Files to create:
- include/ferrum/editor/mesh/mesh_slot.h — mesh_slot_t type, constants, lifecycle API
- include/ferrum/editor/mesh/mesh_edit.h — mesh_edit_t type, selection state, mode enum
- src/editor/mesh/mesh_slot.c — init, destroy, reserve capacity, clear
- src/editor/mesh/mesh_edit.c — init, destroy, set_active_slot, set_mode
- tests/editor/mesh_slot_tests.c
- tests/editor/mesh_edit_tests.c

## Acceptance Criteria

- mesh_slot_t can hold up to 65536 vertices and 196608 indices
- Dynamic buffer growth (reserve doubles capacity)
- mesh_edit_t manages 16 slots with active_slot switching
- Selection bitsets correctly track vertex/edge/face selection
- 5 selection modes: vertex, edge, face, polygroup, object
- All tests pass (init, destroy, reserve, clear, mode switching, slot switching)
- Clean under -Wall -Wextra -Wpedantic
- Max 2 public types per header, max 4 non-static functions per .c file

