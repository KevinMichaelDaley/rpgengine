---
id: rpg-j4q1
status: closed
deps: [rpg-x74u]
links: []
created: 2026-07-06T05:45:03Z
type: task
priority: 1
assignee: KMD
parent: rpg-vy6w
tags: [srd, voxel]
---
# srd-grid-03: seed layout to SDF grid initialization

Convert the grammar's seed layout (room boxes + connectivity from srd_grid_parse) into an srd_sdf_grid_t + srd_room_map_t. For each seed room box: stamp its box SDF into the grid and assign room IDs. For each adjacency pair: carve a doorway between rooms (subtract a thin box at the shared wall). Provides srd_seed_to_grid(seed_boxes, n_rooms, adjacency, grid_out, room_map_out).

## Acceptance Criteria

Test: create 3-room seed with known connectivity, initialize grid, verify rooms are carved (negative voxels), doorways connect rooms (flood-fill from room 1 reaches room 2 through doorway), room IDs correct.

