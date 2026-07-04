---
id: rpg-8gv8
status: closed
deps: [rpg-tuqk]
links: []
created: 2026-07-04T23:00:39Z
type: bug
priority: 0
assignee: KMD
tags: [procgen, grammar, bug, critical]
---
# procgen-fix: ROOM_PENT generates fixed hardcoded shape

## Design

ROOM_PENT always produces a fixed pentagon at (0,0) with radius 5.0. The polygon= parameter is discarded. Room name, floor_z, and ceil_z work but the shape and position are dead code from the coordinate tuple bug (C1).

## Acceptance Criteria

- ROOM_PENT uses polygon= coords from tokenizer\n- ROOM_PENT uses x/y position if provided\n- Default pentagon shape only when no polygon= given\n- Existing tests pass

