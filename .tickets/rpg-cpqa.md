---
id: rpg-cpqa
status: open
deps: [rpg-o9fl]
links: []
created: 2026-07-04T20:37:58Z
type: task
priority: 0
assignee: KMD
parent: rpg-bdrv
tags: [procgen, grammar, blockout]
---
# procgen-1a: ROOM_QUAD rasterizer

## Design

Handle ROOM_QUAD token: parse x,y,w,h,floor_z,ceil_z,name parameters. Rasterize into fr_room_def_t with 4 vertices (rectangle). Validate minimum room size, positive clearance. Write RED test: tests/procgen/procgen_grammar_blockout_room_tests.c.

## Acceptance Criteria

- ROOM_QUAD produces correct 4-vertex polygon\n- Bounds (x,y)+(w,h) map to world-space vertices\n- floor_z/ceil_z stored correctly\n- name field preserved\n- Rooms below minimum size rejected\n- Insufficient clearance (ceil_z <= floor_z) rejected

