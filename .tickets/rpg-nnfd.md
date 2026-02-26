---
id: rpg-nnfd
status: open
deps: []
links: []
created: 2026-02-26T16:34:26Z
type: task
priority: 2
assignee: KMD
tags: [editor, physics]
---
# Editor physics constraints (joints/hinges/springs)

Add editor commands for creating and managing persistent physics constraints between entities. READ FIRST: ref/editor_spec.md, ref/editor_design.md. The physics engine already has a full joint system (phys-800 series). Commands needed: constraint_create (type, entity_a, entity_b, params), constraint_delete (by ID), constraint_edit (modify limits/stiffness/damping), constraint_list. Support ball, hinge, fixed, distance joints. Wire through physics bridge to phys_joint_* APIs. Undo support required. Constraints included in prefab serialization. Files: src/editor/commands/cmd_constraint.c, cmd_constraint_list.c, include/ferrum/editor/edit_constraint.h, src/editor/state/edit_constraint_store.c, tests/editor/cmd_constraint_tests.c

