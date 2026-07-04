---
id: rpg-py13
status: closed
deps: []
links: []
created: 2026-07-04T20:37:58Z
type: task
priority: 0
assignee: KMD
parent: rpg-o9fl
tags: [procgen, grammar, tdd, types]
---
# procgen-0a: Core type definitions

## Design

Create include/ferrum/procgen/procgen_types.h and procgen_layout.h. Define tok_type_t enum (TOK_ROOM_QUAD, TOK_CORRIDOR_H, TOK_SPAWN, TOK_MARKER, TOK_BLOCK, TOK_EBLOCK, etc.), procgen_token_t union (int/float/string value), fr_room_def_t (polygon vertices, floor_z, ceil_z, name), fr_corridor_def_t (from/to points, width, floor_z, ceil_z, angle type), fr_opening_def_t (position, size, type door/window), fr_ramp_def_t, fr_marker_def_t, fr_nav_node_t, fr_nav_edge_t, fr_dungeon_layout_t. All structs properly aligned. Write RED test: tests/procgen/procgen_types_tests.c that validates struct sizes, field offsets, enum values.

## Acceptance Criteria

- procgen_types.h compiles with -Wall -Wextra -Werror\n- procgen_layout.h compiles with -Wall -Wextra -Werror\n- All enums + structs present and documented\n- Tests compile (RED: fail until structs finalized)

