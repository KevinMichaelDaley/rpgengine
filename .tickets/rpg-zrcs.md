---
id: rpg-zrcs
status: closed
deps: [rpg-cpqa, rpg-tdfr, rpg-tn8q, rpg-ft81, rpg-mcsr, rpg-i2vh, rpg-ma6m, rpg-hcur]
links: []
created: 2026-07-04T20:37:59Z
type: task
priority: 0
assignee: KMD
parent: rpg-bdrv
tags: [procgen, grammar, blockout, tdd, integration]
---
# procgen-1i: P1 integration test

rpg-bdrv

## Design

End-to-end test: tokenize a complete multi-room dungeon string with corridors, ramps, doors, spawn, markers, and BLOCK nesting. Rasterize. Verify geometry correctness: room vertex count, corridor extrusion, ramp placement, marker positions, nav graph connectivity. Verify no room overlaps. Test boundary cases.

## Acceptance Criteria

- Full grammar pipeline works end-to-end\n- Geometry verified numerically\n- Nav graph connectivity verified\n- Overlap detection works\n- RED-GREEN-REFACTOR cycle complete

