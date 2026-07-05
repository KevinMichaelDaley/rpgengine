---
id: rpg-0wlk
status: closed
deps: []
links: []
created: 2026-07-05T06:24:14Z
type: epic
priority: 2
assignee: KMD
tags: [procgen]
---
# srd-infra: SRD Infrastructure — extern/SymX submodule, build integration, core types

**DO NOT CREATE STUBS OR PARTIAL IMPLEMENTATIONS; NEVER SKIP FEATURES OR DEFER STEPS!**



Add extern/SymX as a git submodule, integrate with CMake build system, and define the core geometry types (RoomBox, CorridorSeg, StairDef, FloorDef, RoomGraph) that replace the old token-based layout structs.

See design/srd_dungeon_redesign.md for the full architecture.

## Design

## Files

- extern/SymX/ — git submodule
- CMakeLists.txt — add_subdirectory(extern/SymX), target_link_libraries
- include/ferrum/procgen/procgen_srd_types.h — new structs

## Dependencies

None (foundation epic).

## Acceptance Criteria

1. `git submodule status extern/SymX` shows a valid checkout
2. `make srd` (or cmake target) compiles a minimal SymX test without errors
3. procgen_srd_types.h compiles and all structs have correct sizeof and init/destroy functions
4. All existing non-procgen targets still build (libheadless.a, demo_client, etc.)

