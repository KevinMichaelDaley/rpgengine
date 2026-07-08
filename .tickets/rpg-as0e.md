---
id: rpg-as0e
status: closed
deps: [rpg-fuue]
links: []
created: 2026-07-06T05:45:03Z
type: task
priority: 1
assignee: KMD
parent: rpg-vy6w
tags: [srd, voxel, svo]
---
# srd-grid-04: SDF grid to SVO conversion

Booleanize the SDF grid at threshold 0 and populate an npc_svo_grid_t. Walk the grid: voxels with SDF < 0 are air (interior), voxels with SDF >= 0 are solid (wall). The SVO marks SOLID voxels. This replaces procgen_svo_build_from_srd(). Provides srd_sdf_to_svo(grid, svo_out). Must handle grid dimensions that don't match SVO depth (pad or clamp).

## Acceptance Criteria

Test: create a grid with one carved room, convert to SVO, verify solid voxels surround the room interior, verify air voxels inside. Compare node count against expected for a simple box room.

