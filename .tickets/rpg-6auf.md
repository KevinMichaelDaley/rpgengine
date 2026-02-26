---
id: rpg-6auf
status: open
deps: [rpg-ssj2, rpg-3p7n, rpg-nkhw]
links: []
created: 2026-02-26T04:26:35Z
type: task
priority: 1
assignee: KMD
parent: rpg-wxom
tags: [editor, foundation, server]
---
# Basic entity commands (spawn, delete, move, rotate, scale)

Implement the core entity manipulation commands: spawn, delete, move, rotate, scale.

READ FIRST: ref/editor_design.md §2.4 for dispatch table and command format, ref/editor_spec.md §2.4 for deferred API, ref/editor_ux.md §5 for entity operations.

These commands mutate entity state during drain. Each must produce an undo entry so operations can be reversed.

Requirements:
- cmd_spawn: create entity at specified position with type (box/sphere) and optional size. Returns request_id and entity_id in response.
- cmd_delete: remove selected entities. Captures full entity snapshot for undo.
- cmd_move: translate selection by delta vec3.
- cmd_rotate: rotate selection by euler angles.
- cmd_scale: scale selection by factors.
- All commands record inverse operations in the undo stack.
- All commands operate on the editor's selection set (or explicit entity IDs).
- Must interop with existing physics body pool (spawn creates phys body).

Files to create:
- src/editor/commands/cmd_spawn.c
- src/editor/commands/cmd_delete.c
- src/editor/commands/cmd_move.c
- src/editor/commands/cmd_rotate.c
- src/editor/commands/cmd_scale.c
- tests/editor/cmd_entity_tests.c

Dependencies: edit_dispatch, undo stack, selection system

