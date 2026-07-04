---
id: rpg-bdrv
status: open
deps: [rpg-o9fl]
links: []
created: 2026-07-04T20:37:58Z
type: epic
priority: 0
assignee: KMD
tags: [procgen, grammar, tdd, blockout]
---
# procgen: Phase 1 - Blockout Grammar

rpg-o9fl

## Design

Implement the first production grammar: blockout. This grammar handles 4/5-sided polygonal rooms (ROOM_QUAD, ROOM_PENT), axis-aligned and 45°/30°-60° diagonal corridors (CORRIDOR_H, CORRIDOR_V, CORRIDOR_DIAG), floor height ramps (RAMP_UP, RAMP_DOWN), openings (DOOR, WINDOW), spawn points (SPAWN), and named markers (MARKER). Supports BLOCK/EBLOCK nesting for multi-floor structures. Rasterizes tokens into fr_dungeon_layout_t geometry with a generated navigation graph. This is the reference grammar — all future grammars follow this pattern.

## Acceptance Criteria

- ROOM_QUAD: rectangular rooms rasterized correctly with bounds\n- ROOM_PENT: 5-sided convex polygon rooms rasterized\n- CORRIDOR_H/V: axis-aligned corridors with correct extrusion\n- CORRIDOR_DIAG: 45° and 30°-60° diagonal corridors\n- RAMP_UP/DOWN: floor height transitions between rooms\n- DOOR/WINDOW: openings placed at corridor-room junctions\n- SPAWN: single spawn point extracted correctly\n- MARKER: named waypoints with world-space positions\n- BLOCK/EBLOCK: nested structures handled\n- Navigation graph: nodes for each room/junction, edges for connections\n- All rooms non-overlapping (validation)\n- All rooms have positive clearance (ceil_z > floor_z)\n- Full grammar pipeline: token string → layout → verified geometry

