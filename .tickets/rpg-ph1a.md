---
id: rpg-ph1a
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
# Entity property editor in TUI

Implement entity property viewing and editing in the controller TUI.

READ FIRST: ref/editor_design.md §2.4 (properties/inspect commands), ref/editor_ux.md §5.1 (p=properties, i=inspect).

Requirements:
- cmd_properties: show editable property list for selected entity in log area
- cmd_inspect: detailed component dump (all components, all fields)
- Inline editing: cursor to property, type new value, Enter to commit
- Property categories: transform (pos, rot, scale), physics (mass, shape, friction), render (mesh, material)
- Changes go through edit protocol as set_component commands (undo support)

Files to create:
- src/editor/commands/cmd_properties.c
- src/editor/commands/cmd_inspect.c
- src/editor/controller/ctrl_property_edit.c
- tests/editor/cmd_properties_tests.c

