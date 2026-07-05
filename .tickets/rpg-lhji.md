---
id: rpg-lhji
status: closed
deps: [rpg-ynd4, rpg-bux3, rpg-0i0y, rpg-9ug6, rpg-kj6t]
links: []
created: 2026-07-04T20:39:26Z
type: task
priority: 0
assignee: KMD
parent: rpg-8sc6
tags: [procgen, critic, hooks, tdd, integration]
---
# procgen-5f: P5 integration test

rpg-8sc6

## Design

Integration test: set up minimal physics world with player, markers, and death plane. Register all hook types. Simulate: player walks to marker → verify MARKER_HIT fires. Player falls off world → verify FELL_OOB fires. Player stands still → verify STUCK fires. Player takes fatal damage → verify DEATH fires. Timeout reached → verify TIMEOUT fires. Verify multiple simultaneous hooks.

## Acceptance Criteria

- All hook types fire correctly in integration\n- Event data complete and accurate\n- Multiple hooks coexist\n- Hook fire during physics step does not crash\n- RED-GREEN-REFACTOR complete

