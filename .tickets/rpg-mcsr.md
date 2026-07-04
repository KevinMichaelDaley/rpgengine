---
id: rpg-mcsr
status: closed
deps: [rpg-o9fl]
links: []
created: 2026-07-04T20:37:59Z
type: task
priority: 0
assignee: KMD
parent: rpg-bdrv
tags: [procgen, grammar, blockout]
---
# procgen-1e: DOOR/WINDOW rasterizer

## Design

Handle DOOR and WINDOW tokens. An opening is a rectangular void placed at a world-space position, with optional size parameter. Store as fr_opening_def_t. Validate position is on a room-corridor or room-room boundary.

## Acceptance Criteria

- DOOR placed at specified position\n- WINDOW placed at specified position\n- Size parameter (w,h) stored correctly\n- Warning if opening not near any room boundary

