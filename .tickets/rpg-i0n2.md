---
id: rpg-i0n2
status: open
deps: [rpg-tze6, rpg-clq6]
links: []
created: 2026-02-28T22:27:40Z
type: task
priority: 2
assignee: KMD
parent: rpg-37uq
tags: [editor, mesh, tui]
---
# TUI mesh mode (keybindings, visual feedback, mesh stats)

Implement mesh-mode TUI support: keybindings, status display, and visual feedback.

The TUI gains a 'mesh mode' state that activates mesh-specific keybindings and status display. Entering mesh_create or editing an existing mesh entity switches to mesh mode.

Modal keybindings (Vim-inspired):
- 1/2/3/4/5: vertex/edge/face/polygroup/object mode
- g/G: grow/shrink selection (face mode)
- x/l: edge ring/loop (edge mode)
- e: extrude (face mode, prompts for distance)
- i: inset (face mode)
- b: bevel (edge mode)
- c: connect/loop cut (edge mode)
- u: unwrap submenu
- Ctrl+t: triangulate
- Ctrl+q: quadrangulate
- Tab: toggle wireframe
- ~: toggle x-ray selection

Status bar in mesh mode shows:
- Current selection mode (vertex/edge/face/polygroup/object)
- Selection count (e.g., '12 faces selected')
- Mesh stats: vertex count, triangle count, edge count
- Active slot name

Files to create:
- src/editor/controller/ctrl_mesh_mode.c — mesh mode state and keybinding dispatch
- include/ferrum/editor/ctrl_mesh_mode.h — mesh mode type and API
- tests/editor/ctrl_mesh_mode_tests.c

## Acceptance Criteria

- Number keys 1-5 switch selection mode
- Face mode keys (e, i, g, G) dispatch correct commands
- Edge mode keys (b, c, x, l) dispatch correct commands
- Status bar shows current mode and selection count
- Mesh stats update after geometry operations
- Tab toggles wireframe flag
- ~ toggles x-ray flag
- Keys only active in mesh mode (not in normal entity editing)
- Tests: mode switching, key dispatch, status text generation, state transitions

