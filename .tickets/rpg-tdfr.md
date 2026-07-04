---
id: rpg-tdfr
status: open
deps: [rpg-o9fl]
links: []
created: 2026-07-04T20:37:58Z
type: task
priority: 0
assignee: KMD
parent: rpg-bdrv
tags: [procgen, grammar, blockout]
---
# procgen-1b: ROOM_PENT rasterizer

## Design

Handle ROOM_PENT token: parse polygon=((x1,y1),...,(x5,y5)), floor_z, ceil_z, name. Rasterize 5-vertex convex polygon into fr_room_def_t. Validate convexity, minimum area, no self-intersection.

## Acceptance Criteria

- ROOM_PENT produces correct 5-vertex convex polygon\n- Non-convex polygons rejected\n- Self-intersecting polygons rejected\n- Minimum area enforced\n- floor_z/ceil_z stored correctly

