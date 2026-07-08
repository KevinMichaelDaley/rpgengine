---
id: rpg-9pue
status: closed
deps: [rpg-x74u]
links: []
created: 2026-07-06T05:45:35Z
type: task
priority: 1
assignee: KMD
tags: [srd, voxel, rules]
parent: rpg-rtxv  # SRD-E9: Voxel SDF Rewrite Rules
---
# srd-vrule-03: ceiling/floor height rewrite rules

CeilingRaise/CeilingLower: adjust ceiling height by param voxels. FloorStep: raise floor in a sub-region to create a platform. All select (room_id, face=ceil/floor, param). Ceiling raise/lower are inverse pairs. FloorStep inverse removes the step.

## Acceptance Criteria

Tests: raise ceiling, verify room volume increased. Lower it back, verify round-trip. Create floor step, verify platform voxels are solid.

