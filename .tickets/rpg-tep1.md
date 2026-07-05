---
id: rpg-tep1
status: open
deps: [rpg-j5ig]
links: []
created: 2026-07-05T06:26:00Z
type: task
priority: 1
assignee: KMD
parent: rpg-fcyx
tags: [procgen]
---
# srd-013: SVO builder integration -- consume SRD geometry output

**DO NOT CREATE STUBS OR PARTIAL IMPLEMENTATIONS; NEVER SKIP FEATURES OR DEFER STEPS!**



Modify SVO builder to accept RoomBox[], CorridorSeg[], StairDef[]. Remove old token rasterization. RED-phase: tests/procgen/srd/srd_integration_tests.c

## Acceptance Criteria

SVO contains solid voxels; floor complete; corridor connects; stairs generate

