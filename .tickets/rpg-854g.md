---
id: rpg-854g
status: in_progress
deps: [rpg-aj72, rpg-a3dm, rpg-btcr]
links: []
created: 2026-07-05T06:51:26Z
type: task
priority: 0
assignee: KMD
parent: rpg-q5eq
tags: [procgen]
---
# srd-020: Milestone 1 smoke — ASCII parse → RoomGraph integration

DO NOT CREATE STUBS OR PARTIAL IMPLEMENTATIONS; NEVER SKIP FEATURES OR DEFER STEPS!

Integration test after infrastructure (srd-001, srd-002) and ASCII parser (srd-003) are complete. Parse a full multi-floor dungeon ASCII grid (12+ rooms across 2+ floors, multiple room types, stairs between floors, labeled regions) and verify the RoomGraph output is correct: node count, edge count, adjacency correctness, stair anchor identification, label extraction from comments for every room.

RED-phase: tests/procgen/srd/srd_m1_smoke.cpp — parse a 2-floor 12-room grid with B, R, P, G, . chars plus ^/v stairs; verify: exact node count matches expected, every edge connects correct room types, stair anchors have correct (up/down) direction and link to correct rooms, labels extracted from comments resolve to correct room indices.

## Acceptance Criteria

All test grids parse correctly; edge adjacency matches grid topology; stair anchors have correct direction; label extraction from comments works; no memory leaks

