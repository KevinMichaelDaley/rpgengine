---
id: rpg-3p7n
status: closed
deps: [rpg-00d1]
links: []
created: 2026-02-26T04:26:35Z
type: task
priority: 1
assignee: KMD
parent: rpg-wxom
tags: [editor, foundation, server]
---
# Undo/redo stack (edit_undo.c)

Implement the undo/redo stack with dedicated snapshot arena.

READ FIRST: ref/editor_design.md §5 for undo system design (command pattern, inverse commands, group undo, memory strategy), ref/editor_spec.md §2.4 for deferred mutation/undo recording.

Undo entries are recorded at drain time (when mutation executes), not at enqueue time. Delete-undo requires full entity snapshots stored in a dedicated arena.

Requirements:
- undo_stack_t: ring buffer of undo_entry_t (capacity 4096)
- undo_entry_t: forward command, inverse command, group_id, snapshot_data pointer, snapshot_size
- Dedicated snapshot_arena (16 MB default) for entity snapshots
- editor_undo(ctx) / editor_redo(ctx) functions
- Group undo: editor_undo_begin_group / editor_undo_end_group
- When ring wraps, oldest snapshot data is freed from arena
- Arena overflow: force-evict oldest entries until space available
- Undo stack cleared on editor disconnect (does not persist across sessions)

Files to create:
- include/ferrum/editor/edit_undo.h
- src/editor/state/edit_undo.c
- src/editor/commands/cmd_undo.c (undo + redo commands)
- tests/editor/edit_undo_tests.c

Dependencies: json_parse (for snapshot serialization)

