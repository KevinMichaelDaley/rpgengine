---
id: rpg-vmlk
status: closed
deps: [rpg-o8pq]
links: []
created: 2026-03-01T09:58:49Z
type: task
priority: 1
assignee: KMD
parent: rpg-8hc1
tags: [aegis, vm, update]
---
# Aegis update set construction instructions

Implement update set construction bytecode instructions per ref/aegis_bytecode_spec.md §3.3, §2.3.

These instructions build an aegis_update_set_t that maps to the engine's script_env_write_attr() for applying updates via script_update_buffer_t.

Instructions:
- build_update r_dst: create empty update builder; r_dst holds builder state (uses a register slot to track current update)
- target_entity r_upd, r_entity_id: set target entity ID for the current update
- set_field r_upd, key, r_val: add attribute write; key is SCRIPT_KEY_* immediate; value copied from register (type inferred from key or explicit)
- add_hint r_upd, hint_type: add validation hint flag (HINT_MOVEMENT, HINT_AUTHORITY, HINT_PREDICTION)
- push_update r_upd: finalize current update and append to update_set; enforces max_updates limit (default 1024)

The update_set is accumulated across the script's execution and returned to the engine on yield.

Files:
- src/aegis/ops/aegis_ops_update.c (build_update, target_entity, set_field, add_hint)
- src/aegis/ops/aegis_ops_update_push.c (push_update + limit enforcement)
- tests/aegis/aegis_ops_update_tests.c

Acceptance criteria:
- [ ] build_update → target_entity → set_field → push_update produces correct aegis_state_update_t
- [ ] Multiple set_field calls on same update accumulate correctly
- [ ] Validation hints stored as flags on the update
- [ ] push_update enforces max_updates limit; excess → error
- [ ] Update set correctly returned to engine on yield
- [ ] Tests: single update, multiple fields, multiple entities, hint flags, max updates exceeded

## Acceptance Criteria

Update builder produces correct aegis_update_set_t entries, limit-enforced, hints attached

