---
id: rpg-dltg
status: open
deps: [rpg-bdrv]
links: []
created: 2026-07-04T20:39:25Z
type: task
priority: 0
assignee: KMD
parent: rpg-6p4q
tags: [procgen, grammar, serializer]
---
# procgen-2a: Layout → JSON converter

## Design

Implement procgen_serialize_to_json() that converts fr_dungeon_layout_t to the existing JSON level format. Map room geometry → func_geo entity entries, corridor geometry → func_geo entities, markers → info_target entities, spawn → info_player_start. Use existing JSON conventions from edit_level_deserialize. Write RED test: tests/procgen/procgen_serialize_tests.c.

## Acceptance Criteria

- JSON output is valid\n- JSON conforms to existing level format\n- All geometry types present in output\n- Entity positions correct relative to world-space

