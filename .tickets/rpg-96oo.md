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

Implement the C→Lua API bindings for entity manipulation.

READ FIRST: ref/editor_design.md §6.4 for binding pattern (l_spawn_box example), ref/editor_ux.md §8.3 for full script API listing.

All entity operations from Lua are deferred — they go through the editor command system so they are undo-recorded and executed during drain.

Requirements:
- spawn_box(pos, size) → entity_id
- spawn_sphere(pos, radius) → entity_id
- spawn_prefab(pos, path) → entity_id
- spawn_mesh(pos, mesh_path) → entity_id
- destroy(entity_id)
- move(entity_id, delta)
- set_pos(entity_id, pos)
- set_rot(entity_id, quat)
- select(entity_id) / deselect_all()
- find_all() → table of entity_ids
- find_where(component, field, op, value) → table
- get_component(entity_id, name) → table

All functions push deferred commands, recorded in undo stack.

Files to create:
- src/editor/script/lua_entity_api.c
- tests/editor/lua_entity_api_tests.c

