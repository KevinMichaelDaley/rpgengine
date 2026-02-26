---
id: rpg-nkhw
status: closed
deps: [rpg-ssj2]
links: []
created: 2026-02-26T04:26:35Z
type: task
priority: 1
assignee: KMD
parent: rpg-wxom
tags: [editor, foundation, server]
---
# Selection system (edit_selection.c)

Implement the entity selection system on the server side.

READ FIRST: ref/editor_spec.md §7.2 for client-side selection, ref/editor_design.md §2.4 for select command, ref/editor_ux.md §5.4 for multi-select operations.

The selection is a set of entity IDs maintained by the server. Commands like delete, move, rotate operate on the selection. The client mirrors selection state for highlight rendering.

Requirements:
- edit_selection_t: a set of entity IDs (bitfield or hash set)
- cmd_select: add/remove/toggle/set entities. Supports: select by ID, select all, select none, select where (component query).
- Selection changes are broadcast to connected clients (via response ring → I/O thread → client state socket).
- Maximum selection size: 4096 entities.
- Thread-safe: only mutated during drain on main tick thread.

Files to create:
- include/ferrum/editor/edit_selection.h
- src/editor/state/edit_selection.c
- src/editor/commands/cmd_select.c
- tests/editor/edit_selection_tests.c

Dependencies: edit_dispatch

