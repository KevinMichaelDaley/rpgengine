---
id: rpg-ft81
status: open
deps: [rpg-o9fl]
links: []
created: 2026-07-04T20:37:59Z
type: task
priority: 0
assignee: KMD
parent: rpg-bdrv
tags: [procgen, grammar, blockout]
---
# procgen-1d: RAMP_UP/RAMP_DOWN rasterizer

## Design

Handle RAMP_UP and RAMP_DOWN tokens. A ramp is a sloped surface connecting two floor heights. Parse from/to points and dz (height change for RAMP_UP, negative for RAMP_DOWN). Rasterize as a connecting volume with sloped floor.

## Acceptance Criteria

- RAMP_UP: positive dz, floor slopes upward\n- RAMP_DOWN: negative dz, floor slopes downward\n- Ramp connects two distinct vertical levels\n- Ramp geometry correct (sloped quad volume)

