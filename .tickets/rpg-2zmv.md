---
id: rpg-2zmv
status: open
deps: []
links: []
created: 2026-02-26T04:26:35Z
type: task
priority: 1
assignee: KMD
parent: rpg-wxom
tags: [editor, foundation, client]
---
# 3D cursor rendering on client

Implement 3D cursor rendering in the editor client.

READ FIRST: ref/editor_design.md §4 for cursor implementation (state, rendering, synchronization), ref/editor_ux.md §4 for cursor appearance and keyboard movement, ref/editor_spec.md §7.2 for client-side module table.

The cursor is a visual indicator in the 3D viewport showing where editor operations will take place.

Requirements:
- editor_cursor_t: position (vec3), grid_size, snap_enabled, visible
- Render as: three axis-colored lines (X=red, Y=green, Z=blue, length=2*grid_size), small yellow sphere at center, grid-cell highlight on XZ plane
- Uses existing debug line rendering path (no new shaders)
- Cursor state owned by client, updated by commands from controller (via client state socket)
- Grid snap logic: round position components to nearest grid_size multiple
- Cursor visibility toggle
- Must integrate with existing client render loop (SDL + GL)
- Gated behind EDITOR_ENABLE compile flag

Files to create:
- include/ferrum/editor/client/client_cursor.h
- src/editor/client/client_cursor.c
- src/editor/client/client_cursor_render.c

Dependencies: existing debug line renderer

