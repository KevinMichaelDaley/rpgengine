---
id: rpg-qa29
status: closed
deps: []
links: []
created: 2026-02-26T16:34:25Z
type: task
priority: 2
assignee: KMD
tags: [editor]
---
# Object grouping (group/ungroup selected entities)

Add editor commands for grouping and ungrouping entities. READ FIRST: ref/editor_spec.md, ref/editor_design.md. Commands: group (create named group from selection), ungroup (dissolve group), select_group (select all in group). Groups have name, pivot, optional parent. Nested groups supported. Ops on group apply to all members. Serialized in save/load and prefabs. Undo support. Tab-completion for group names. Files: src/editor/state/edit_group.c, include/ferrum/editor/edit_group.h, src/editor/commands/cmd_group.c, cmd_ungroup.c, tests/editor/edit_group_tests.c

