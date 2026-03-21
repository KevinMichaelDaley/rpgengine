---
id: rpg-97dt
status: closed
deps: []
links: []
created: 2026-03-12T06:48:51Z
type: task
priority: 2
assignee: KMD
parent: rpg-zwcc
---
# §2.1 Undo System

See ref/scene_editor_design.md §2.1. undo_stack_t (doubly-linked list), push/pop, undo/redo, branching (rebase displaced records), conflict detection (entity ID, vertex range), rebase logic, :undo tree command, orphan branch recovery.

## Acceptance Criteria

Ctrl+Z/Ctrl+Shift+Z undo/redo working. Branching works. :undo tree shows structure. Cross-reference with ref/scene_editor_spec.md and ref/scene_editor_ux.md for requirements and interaction details.

