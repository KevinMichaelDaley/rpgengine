---
id: rpg-hcur
status: closed
deps: [rpg-o9fl, rpg-cpqa]
links: []
created: 2026-07-04T20:37:59Z
type: task
priority: 0
assignee: KMD
parent: rpg-bdrv
tags: [procgen, grammar, blockout, nav]
---
# procgen-1h: Navigation graph generation

## Design

After rasterizing all geometry, generate a navigation graph: each room/junction becomes a nav node, each corridor/ramp becomes a nav edge connecting two nodes. Compute connectivity from corridor endpoints to room boundaries. Store in fr_dungeon_layout_t.nav_graph. Write RED test verifying connectivity.

## Acceptance Criteria

- Each room produces a nav node\n- Each corridor produces a nav edge between rooms\n- Nav graph is connected (all nodes reachable from spawn room)\n- Edge weights represent travel distance

