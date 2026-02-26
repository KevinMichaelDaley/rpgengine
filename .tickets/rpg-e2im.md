---
id: rpg-e2im
status: open
deps: [rpg-96oo]
links: []
created: 2026-02-26T04:28:43Z
type: task
priority: 2
assignee: KMD
parent: rpg-p9zq
tags: [editor, scripting, server]
---
# Script undo grouping (begin_group/end_group)

Implement undo grouping so multi-entity script operations can be undone atomically.

READ FIRST: ref/editor_design.md §5.3 for group undo, ref/editor_spec.md §2.4 for undo recording at drain time.

Requirements:
- editor_undo_begin_group(ctx) / editor_undo_end_group(ctx) bracket script execution
- All undo entries within a group share the same group_id
- editor_undo(ctx) undoes the entire group in one step
- Lua API: undo_group_begin() / undo_group_end() exposed to scripts
- Automatic grouping: cmd_run wraps entire script execution in a group
- Nested groups flatten (only outermost group_id matters)

Files to create:
- Modify src/editor/state/edit_undo.c (add group logic)
- src/editor/script/lua_undo_api.c
- tests/editor/edit_undo_group_tests.c

