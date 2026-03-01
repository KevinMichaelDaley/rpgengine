---
id: rpg-96oo
status: open
deps: [rpg-zqex]
links: []
created: 2026-02-26T04:28:42Z
type: task
priority: 2
assignee: KMD
parent: rpg-p9zq
tags: [editor, scripting, server]
---
# Lua entity manipulation API bindings

Implement the Lua API bindings for entity manipulation via script_env_t.

READ FIRST: ref/editor_design.md §6.4-6.5 for the native/Lua interface pattern, ref/editor_ux.md §8.3 for full script API listing.

Entity reads come from the frozen snapshot (script_env_t.entities). Entity
writes go through the update buffer (script_env_t.updates) for property
changes, or through the SPSC command ring (script_env_t.cmd_ring) for
structural operations (spawn, delete, group). All structural operations
are undo-recorded on the main tick thread when drained.

Requirements:
- spawn_box(pos, size) → submits spawn command via cmd_ring
- spawn_sphere(pos, radius) → submits spawn command via cmd_ring
- destroy(entity_id) → submits delete command via cmd_ring
- move(entity_id, delta) → writes to update buffer (SCRIPT_UPD_POS)
- set_pos(entity_id, pos) → writes to update buffer (SCRIPT_UPD_POS)
- set_rot(entity_id, quat) → writes to update buffer (SCRIPT_UPD_ROT)
- set_scale(entity_id, scale) → writes to update buffer (SCRIPT_UPD_SCALE)
- select(entity_id) / deselect_all() → submits via cmd_ring
- get_entities() → returns table from snapshot (read-only)
- find_where(field, op, value) → filters snapshot, returns table

Files to create:
- src/editor/script/lua_entity_api.c
- tests/editor/lua_entity_api_tests.c

