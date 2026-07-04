---
id: rpg-i2vh
status: open
deps: [rpg-o9fl]
links: []
created: 2026-07-04T20:37:59Z
type: task
priority: 0
assignee: KMD
parent: rpg-bdrv
tags: [procgen, grammar, blockout]
---
# procgen-1f: SPAWN/MARKER rasterizer

## Design

Handle SPAWN and MARKER tokens. SPAWN: parse x,y,z → fr_dungeon_layout_t.spawn_pos. MARKER: parse x,y,z,name → fr_marker_def_t appended to markers array. Validate exactly one SPAWN token exists. Validate MARKER names are non-empty and distinct.

## Acceptance Criteria

- Single SPAWN position extracted correctly\n- Multiple SPAWN tokens rejected (error)\n- Zero SPAWN tokens rejected (error)\n- MARKER names stored correctly\n- Duplicate MARKER names rejected\n- At least 3 MARKERs validated (by grammar spec, not tokenizer)

