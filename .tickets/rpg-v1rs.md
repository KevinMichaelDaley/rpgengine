---
id: rpg-v1rs
status: closed
deps: [rpg-eccf, rpg-dvgo, rpg-vmlk]
links: []
created: 2026-03-01T05:35:41Z
type: task
priority: 2
assignee: KMD
parent: rpg-p9zq
tags: [editor, scripting, server]
---
# Script rebase (apply entity updates onto tick state)

Implement the rebase logic that applies script entity updates on top of physics/game state during the main tick's commit phase.

READ FIRST: ref/editor_design.md §6.3 for rebasing description and ordering.

The rebase reads the front update buffer (variable-length blob of entity updates with key-value attribute writes) and applies each written attribute to the authoritative entity state. Only attributes explicitly written by the script are applied — everything else retains physics/native results.

Requirements:
- script_rebase_apply(store, ecs_world, update_blob, used_bytes) — iterate packed updates, apply attrs
- For edit entities: map SCRIPT_KEY_POS→pos, SCRIPT_KEY_ROT→rot, etc. directly; dynamic attrs → entity_attrs_t block
- For ECS entities (generation > 0): map attribute keys back to sparse set components
- Handle missing entities gracefully (entity deleted between snapshot and rebase)
- Handle type mismatches gracefully (log warning, skip)
- Clear the front buffer after rebase

Files to create:
- src/editor/script/edit_script_rebase.c (rebase_apply)
- tests/editor/edit_script_rebase_tests.c

