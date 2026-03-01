---
id: rpg-eccf
status: closed
deps: [rpg-dvgo]
links: []
created: 2026-03-01T05:35:19Z
type: task
priority: 2
assignee: KMD
parent: rpg-p9zq
tags: [editor, scripting, server]
---
# Script environment and snapshot (script_env_t)

Implement script_env_t, script_entity_snapshot_t, and the snapshot builder that copies entity state into a read-only view for the script thread.

READ FIRST: ref/editor_design.md §6.2 for all type definitions, §6.3 for snapshot flow.

This module owns the data structures that the script thread reads (snapshots) and writes (update blob). It does NOT own the thread — that is rpg-zqex.

Requirements:
- script_entity_snapshot_t: entity_id, generation, active, type, name, pos, rot, scale, body_index, materials, entity_attrs_t attrs
- script_entity_view_t: pointer + count + capacity
- script_env_t: entities view, update_blob (write), cmd_ring ptr, cursor/selection context, runtime backptr
- script_env_write_attr(env, entity_id, gen, key, type, data, size) — append variable-length attr write to update blob
- script_entity_get_attr(snapshot_entity, key, out_type, out_size) — read dynamic attr from snapshot
- script_update_buffer_t: double-buffered blob with atomic ready flag, swap function
- Snapshot builder: iterate edit_entity_store + ECS world → fill snapshot array (copies fixed fields + dynamic attrs)
- script_env_init(env, capacity) / script_env_reset(env)

Files to create:
- include/ferrum/editor/edit_script_env.h (script_env_t, snapshot types, update buffer)
- src/editor/script/edit_script_env.c (env init/reset, snapshot builder)
- src/editor/script/edit_script_update.c (write_attr, update blob management)
- tests/editor/edit_script_env_tests.c

