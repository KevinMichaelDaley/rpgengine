---
id: rpg-tn8q
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
# procgen-1c: CORRIDOR_H/V/DIAG rasterizer

## Design

Handle CORRIDOR_H, CORRIDOR_V, CORRIDOR_DIAG tokens. Horizontal: y values must match. Vertical: x values must match. Diagonal: 45° or 30°/60° angles. Each corridor is an extruded line segment of width w, bounded by floor_z/ceil_z. Validate angle constraints for diagonal corridors.

## Acceptance Criteria

- CORRIDOR_H: y1==y2 enforced, x-axis corridor\n- CORRIDOR_V: x1==x2 enforced, y-axis corridor\n- CORRIDOR_DIAG: 45° or 30°/60° angle enforced\n- Width extrusion produces correct 4-vertex polygon\n- Invalid angles rejected with clear message

