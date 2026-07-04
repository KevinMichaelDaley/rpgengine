---
id: rpg-7rgr
status: open
deps: []
links: []
created: 2026-07-04T23:00:39Z
type: task
priority: 2
assignee: KMD
tags: [procgen, nav, feature]
---
# procgen-fix: navigation graph never generated

## Design

fr_dungeon_layout_t declares nav_nodes[], nav_edges[] with counts but no rasterizer ever populates them. The design doc says the navigation graph is critical for the Critic playtester to validate marker reachability.

## Acceptance Criteria

- Nav nodes created for each room (centroid)\n- Nav edges created connecting adjacent rooms via corridors\n- Nav graph populated in fr_dungeon_layout_t\n- Connected rooms have edges\n- Existing tests pass

