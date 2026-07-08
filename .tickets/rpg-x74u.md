---
id: rpg-x74u
status: closed
deps: [rpg-fuue]
links: []
created: 2026-07-06T05:45:03Z
type: task
priority: 1
assignee: KMD
parent: rpg-vy6w
tags: [srd, voxel]
---
# srd-grid-02: room identity grid (per-voxel room IDs + adjacency)

Add srd_room_map_t: a parallel uint8 grid of per-voxel room ownership (0=wall/void, 1..N=room ID), room count, adjacency matrix, and per-room type array. Provide init/destroy, stamp_room (assign room ID to voxels inside a box region), query room_at_voxel, compute_adjacency (scan for neighbouring voxels with different room IDs), and count_room_volume (count voxels with a given room ID). This is the room identity layer that rewrite rules use to know which voxels belong to which room.

## Acceptance Criteria

Tests: init/destroy, stamp two rooms, verify IDs, compute adjacency detects shared wall, volume counts correct. Files: srd_room_map.h, srd_room_map.c, tests/srd_room_map_tests.c.

