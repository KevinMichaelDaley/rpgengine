---
id: rpg-b51c
status: open
deps: []
links: [rpg-894b]
created: 2026-03-21T03:46:14Z
type: task
priority: 3
assignee: kmd
---
# Undo orphan branch recovery (:undo recover <branch_id>)

Per ref/scene_editor_design.md §2.1 item 9: when branching undo moves conflicting entries to an orphan branch, the user should be able to recover and re-apply them via `:undo recover <branch_id>`.

The orphan branch storage (`undo_branches_t`) and `:undo tree` display already exist. This ticket adds the command to re-apply an orphan branch's entries.

## Deliverables

- `cmd_undo_recover.c` — command handler that takes a branch index, iterates its entries, and re-applies them as new undo records
- Register as `undo_recover` in dispatch + TUI command defs (alias `ur`)
- Conflict check: if recovering entries conflict with current HEAD state, warn or abort
- Tests: recover non-conflicting orphan, recover with new conflicts, invalid branch_id

## Implementation notes

- Orphan branch entries have stale snapshot_data pointers (arena compacted). Recovery of delete-undo (spawn from snapshot) may not be possible — warn the user
- Branch is consumed (cleared) after successful recovery
- `:undo tree` should show recovered entries in the main branch after recovery
