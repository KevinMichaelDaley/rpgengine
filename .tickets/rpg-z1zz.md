---
id: rpg-z1zz
status: closed
deps: [rpg-ndhj, rpg-gc07, rpg-9bbw]
links: []
created: 2026-07-05T06:51:26Z
type: task
priority: 0
assignee: KMD
parent: rpg-q5eq
tags: [procgen]
---
# srd-023: Milestone 4 smoke — SRD optimization on simple grid

DO NOT CREATE STUBS OR PARTIAL IMPLEMENTATIONS; NEVER SKIP FEATURES OR DEFER STEPS!

Integration test after grammar (srd-009), rewrite engine (srd-010), and optimizer (srd-011) are complete. Take a full multi-floor dungeon ASCII grid with LOSS expression (3 floors, 15+ rooms total, 5+ corridors, 2 stair pairs, multiple room types with different adjacency/visibility/distance constraints), run SRD for a full optimization budget. Verify: optimizer converges (energy decreases monotonically), structural rewrites of every type are proposed and some are accepted when beneficial, final geometry satisfies all loss constraints simultaneously, rooms are properly sized, corridors connect designated rooms, stairs align across floors.

RED-phase: tests/procgen/srd/srd_m4_srd_smoke.cpp — 3-floor 15-room ASCII + multi-term LOSS (PathDistance, LineOfSight, Separation, NonPenetration all active); run SRD for 2000 steps or 3s budget; verify energy after SRD is <10% of initial energy; verify at least 2 distinct rewrite types were accepted; verify final geometry has all required rooms present, all corridors connecting correct rooms, stairs at aligned positions across floors.

## Acceptance Criteria

Energy after SRD is <10% of initial energy; at least 2 distinct rewrite types accepted (e.g., split + add-connection); final geometry has all 15+ rooms present with correct adjacency; all corridors connect correct room pairs; stairs align in XZ across floor boundaries; no crashes, NaN, or infinite values; time budget is respected (stops cleanly at limit)

