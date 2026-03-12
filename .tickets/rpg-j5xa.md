---
id: rpg-j5xa
status: open
deps: []
links: []
created: 2026-03-12T06:48:51Z
type: task
priority: 2
assignee: KMD
parent: rpg-zwcc
---
# §2 Engine: Server-Side Undo Stack

See ref/scene_editor_design.md Engine-Side Work table. Server-side undo stack in src/editor/protocol/edit_undo_stack.c.

## Acceptance Criteria

Server tracks undo history. Undo/redo commands processed server-side. Cross-reference with ref/scene_editor_spec.md and ref/scene_editor_ux.md for requirements and interaction details.

