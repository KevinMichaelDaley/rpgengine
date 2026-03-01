---
id: rpg-kuw1
status: closed
deps: [rpg-zqex]
links: []
created: 2026-03-01T05:36:33Z
type: task
priority: 2
assignee: KMD
parent: rpg-p9zq
tags: [editor, scripting, server]
---
# Script undo grouping (begin_group/end_group)

Implement undo grouping for script operations so that a single script execution can be undone as one atomic operation.

Requirements:
- begin_group(label) / end_group() commands submitted via cmd_ring
- When main tick drains begin_group: start collecting undo records into a compound undo entry
- When main tick drains end_group: seal the compound entry as a single undo step
- Nested groups NOT supported (begin while already in group is an error)
- If script crashes between begin/end: auto-close group, mark as partial
- eval and run commands auto-wrap in a group (implicit begin/end around script execution)
- Undo of a grouped operation reverses all entity changes in reverse order

Files to create:
- src/editor/undo/edit_undo_group.c (group begin/end, compound entry)
- tests/editor/edit_undo_group_tests.c


## Notes

**2026-03-01T09:58:49Z**

Superseded by Aegis VM implementation. See ref/aegis_bytecode_spec.md.
