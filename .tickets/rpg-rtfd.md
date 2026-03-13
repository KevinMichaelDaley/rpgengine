---
id: rpg-rtfd
status: open
deps: []
links: []
created: 2026-02-26T04:30:12Z
type: task
priority: 2
assignee: KMD
parent: rpg-b5ma
tags: [editor, polish, client, controller]
---
# Camera commands (front/right/top/ortho/position)

Implement camera control commands sent from controller to client.

READ FIRST: ref/editor_design.md §2.4 dispatch table (cmd_camera entry), ref/editor_ux.md §4.4 for camera commands.

Camera state lives on the client. Commands are sent via the client state socket (not the server — camera is purely a view concern).

Requirements:
- camera front: align camera to -Z axis looking at cursor
- camera right: align camera to +X axis
- camera top: align camera to -Y axis
- camera ortho: toggle orthographic/perspective projection
- camera pos x y z: set camera position explicitly
- Numpad shortcuts: 1=front, 3=right, 7=top, 5=ortho toggle, 0=reset
- Smooth transition animation (lerp over ~200ms)

Files to create:
- src/editor/client/client_editor_camera.c
- include/ferrum/editor/client/client_editor_camera.h
- src/editor/controller/ctrl_camera.c
- tests/editor/client_editor_camera_tests.c


## Notes

**2026-03-13T04:09:26Z**

Scope reduced: camera view shortcuts (1/3/7/5 for front/right/top/ortho) and smooth transitions moved to Phase 1 §1.1 (rpg-dzgo). This ticket now covers ONLY: camera pos x y z explicit positioning command, camera reset (0 key), and any additional camera TUI commands not covered by Phase 1 viewport controls.
