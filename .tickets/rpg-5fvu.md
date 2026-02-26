---
id: rpg-5fvu
status: open
deps: []
links: []
created: 2026-02-26T04:27:44Z
type: task
priority: 2
assignee: KMD
parent: rpg-tiet
tags: [editor, assets, server]
---
# Clone command (entity duplication)

Implement the clone command for duplicating selected entities.

READ FIRST: ref/editor_design.md §2.4 dispatch table (cmd_clone entry), ref/editor_ux.md §5.1 (c key = clone).

Requirements:
- cmd_clone: duplicate all selected entities at cursor position (or with offset)
- Deep copy: all components, materials, physics properties
- New entities get unique IDs
- New entities become the new selection
- Undo support (delete all cloned entities)
- Supports numeric prefix: '3c' clones 3 times with grid-unit offsets

Files to create:
- src/editor/commands/cmd_clone.c
- tests/editor/cmd_clone_tests.c

