---
id: rpg-33ae
status: open
deps: []
links: []
created: 2026-02-26T04:30:48Z
type: task
priority: 3
assignee: KMD
parent: rpg-l5le
tags: [editor, advanced, client]
---
# Gizmo rendering (translate/rotate/scale handles)

Implement interactive gizmo rendering for translate, rotate, and scale handles in the viewport.

READ FIRST: ref/editor_spec.md §7.2 (client modules), ref/editor_ux.md §5 (entity operations).

Requirements:
- Translate gizmo: three colored arrows (X=red, Y=green, Z=blue) at selection center
- Rotate gizmo: three colored circles around selection
- Scale gizmo: three colored cubes at arrow tips
- Mouse interaction: click+drag on axis handle to constrain operation
- Gizmo mode toggled by key (translate=w, rotate=e, scale=r — configurable)
- Uses existing debug line rendering or a thin overlay shader
- Gizmo size scales with camera distance (constant screen size)

Files to create:
- src/editor/client/client_gizmo.c
- include/ferrum/editor/client/client_gizmo.h
- src/editor/client/client_gizmo_render.c
- tests/editor/client_gizmo_tests.c

