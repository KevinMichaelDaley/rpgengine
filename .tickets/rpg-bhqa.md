---
id: rpg-bhqa
status: open
deps: []
links: []
created: 2026-02-26T04:30:12Z
type: task
priority: 2
assignee: KMD
parent: rpg-b5ma
tags: [editor, polish, server]
---
# Prefab system (save/load entity templates)

Implement the prefab system for saving and reusing entity templates.

READ FIRST: ref/editor_spec.md §2.2 for asset types (prefabs), ref/editor_ux.md §5.2 for spawn prefab workflow.

Requirements:
- Prefab format: JSON file containing entity description (type, components, children)
- Save selection as prefab: serialize selected entities (with relative positions) to a .prefab.json file
- Spawn prefab: load .prefab.json, instantiate all entities at cursor position
- Prefabs registered in asset registry (browsable, tab-completable)
- Nested prefabs (prefab referencing other prefabs)
- Undo support for prefab spawn (group undo for all spawned entities)

Files to create:
- src/editor/assets/edit_prefab.c
- include/ferrum/editor/edit_prefab.h
- src/editor/commands/cmd_prefab.c
- tests/editor/edit_prefab_tests.c

