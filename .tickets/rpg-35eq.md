---
id: rpg-35eq
status: closed
deps: [rpg-tuqk]
links: []
created: 2026-07-04T23:00:39Z
type: bug
priority: 0
assignee: KMD
tags: [procgen, grammar, bug, critical]
---
# procgen-fix: corridor/opening/ramp positions hardcoded

## Design

Corridors always from=(0,0)→(0,0), openings at (0,0,0), ramps at fixed positions. All due to coordinate tuple parsing bug (C1). After C1 is fixed, these rasterizers need to actually read the position params.

## Acceptance Criteria

- Corridor from=(x1,y1) to=(x2,y2) works\n- DOOR at=(x,y) and WINDOW at=(x,y) work\n- RAMP_UP/DOWN from=(x1,y1) to=(x2,y2) works\n- Existing tests pass

