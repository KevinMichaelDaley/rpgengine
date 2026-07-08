---
id: rpg-fuue
status: closed
deps: []
links: []
created: 2026-07-06T05:45:03Z
type: task
priority: 1
assignee: KMD
parent: rpg-vy6w
tags: [srd, voxel]
---
# srd-grid-01: srd_sdf_grid_t type, CSG stamp ops, and tests

Define srd_sdf_grid_t (dense float grid with nx/ny/nz, voxel_size, origin). Implement init/destroy, get/set, fill, copy, count_negative. Implement CSG stamp primitives: stamp_box (union=min), subtract_box (max(g,-sdf)), stamp_sphere, subtract_sphere. Inline world<->voxel coordinate helpers. 21 tests covering lifecycle, bounds, CSG union of overlapping boxes, sphere subtract, clipping, copy independence.

## Acceptance Criteria

21/21 tests pass. Clean compile under -Wall -Wextra. Files: srd_sdf_grid.h, srd_sdf_grid.c (4 funcs), srd_sdf_grid_ops.c (3 funcs), srd_sdf_grid_stamp.c (4 funcs), tests/srd_sdf_grid_tests.c.

