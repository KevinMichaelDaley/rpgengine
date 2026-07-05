---
id: rpg-omqm
status: open
deps: [rpg-j5ig, rpg-tep1]
links: []
created: 2026-07-05T06:51:26Z
type: task
priority: 0
assignee: KMD
parent: rpg-q5eq
tags: [procgen]
---
# srd-024: Milestone 5 smoke — full pipeline (ASCII → SVO → mesh)

DO NOT CREATE STUBS OR PARTIAL IMPLEMENTATIONS; NEVER SKIP FEATURES OR DEFER STEPS!

Integration test after bridge (srd-012) and SVO integration (srd-013) are complete. Run the full pipeline end-to-end on a complete dungeon: a 3-floor 15+ room ASCII grid with multi-term LOSS expression → srd_generate() → SVO builder → chunk mesh. Verify the output mesh has valid geometry at scale: every room is hollow with floors/ceilings/walls, all corridors connect their designated rooms, stairs generate correct stepped geometry across floors, mesh faces have correct winding, no SVO initialization errors, room interiors are traversable (no wall blocks between connected rooms).

RED-phase: tests/procgen/srd/srd_m5_pipeline_smoke.cpp — 3-floor 15-room ASCII + multi-term LOSS → srd_generate() → SVO → mesh; verify solid voxel count is reasonable for the prompt (within expected order of magnitude given room count and world extent; not zero, not saturating the grid), verify mesh bounds are consistent with the world extent (not collapsed to a point, not extending beyond world), verify all room floor voxels exist at correct Y levels, verify corridor openings into rooms are clear (no blocking wall voxels), verify stair step voxels exist between floors, verify face winding matches engine reference for first 6 faces of each material type.

## Acceptance Criteria

Solid voxel count is within reasonable order of magnitude for a 3-floor 15-room dungeon (not zero, not saturating the grid); mesh bounds are consistent with the world extent (not collapsed to a point, not extending beyond ±world_extent); mesh faces have correct winding for all 6 face directions; all 15+ rooms are hollow (floor slabs, ceiling slabs, wall columns on perimeter); all corridors connect their designated room interiors with clear openings; stairs generate correct stepped geometry between floors; room interiors are traversable (no wall blocks at corridor entrance points); no SVO initialization errors

