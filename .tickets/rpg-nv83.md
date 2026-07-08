---
id: rpg-nv83
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
# srd-vrule-01: wall push/pull/bevel/niche rewrite rules

Implement 4 wall rules operating on srd_sdf_grid_t + srd_room_map_t. WallPush: offset a room's wall face inward by param voxels (add to SDF along face). WallPull: offset outward (inverse of push). WallBevel: bevel the wall-ceiling or wall-floor edge. WallNiche: carve a rectangular niche into a wall face. Each rule selects (room_id, face=N/S/E/W, param). All are invertible: push<->pull, bevel<->unbevel, niche<->fill_niche.

## Acceptance Criteria

Tests: apply WallPush, verify wall moved inward (voxels that were inside are now outside). Apply inverse, verify round-trip. Same for all 4 rules.

