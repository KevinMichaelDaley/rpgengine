---
id: rpg-a3dm
status: in_progress
deps: [rpg-aj72]
links: []
created: 2026-07-05T06:25:21Z
type: task
priority: 0
assignee: KMD
parent: rpg-0wlk
tags: [procgen]
---
# srd-002: procgen_srd_types.h -- geometry type structs

**DO NOT CREATE STUBS OR PARTIAL IMPLEMENTATIONS; NEVER SKIP FEATURES OR DEFER STEPS!**



Define the C structs that replace fr_dungeon_layout_t: RoomBox, CorridorSeg, StairDef, FloorDef, RoomGraph. RED-phase: tests/procgen/srd/srd_types_tests.c

## Acceptance Criteria

Compiles -Wall -Wextra -Werror, correct sizeof, init/destroy leak-free

