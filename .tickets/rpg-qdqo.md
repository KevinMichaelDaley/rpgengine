---
id: rpg-qdqo
status: open
deps: []
links: []
created: 2026-02-26T04:30:48Z
type: task
priority: 3
assignee: KMD
parent: rpg-l5le
tags: [editor, advanced, controller]
---
# Context menu mode in TUI

Implement the context menu as a modal overlay in the controller TUI.

READ FIRST: ref/editor_ux.md §4.5 for context menu specification (modal overlay, key shortcuts).

Requirements:
- Right-click in viewport sends context_menu event to controller (via client state socket)
- Event includes clicked position and entity (if any)
- Controller displays modal overlay: [s] Spawn here, [p] Properties, [d] Delete, [g] Grab, [m] Material, [Esc] Cancel
- Context-sensitive: different options depending on what was clicked (entity vs empty space)
- Modal mode: only listed keys active while menu visible
- Selecting an option executes the command and returns to Normal mode
- Escape dismisses without action

Files to create:
- src/editor/controller/ctrl_context_menu.c
- tests/editor/ctrl_context_menu_tests.c

