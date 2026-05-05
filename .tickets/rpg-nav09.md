---
id: rpg-nav09
status: closed
deps: [rpg-nav04]
links: [rpg-nav02, rpg-nav04]
created: 2026-05-03T20:00:00Z
type: bug
priority: 1
assignee: KMD
parent: rpg-nav01
tags: [navigation, bug, pathfinding, agent-dimensions]
---
# Agent Dimensions Ignored Across Pathfinding Pipeline

`agent_radius` and `agent_height` are passed through the API but ignored everywhere:

| File | Line | Issue |
|------|------|-------|
| `npc_svo_floodfill.c` | 63,87 | `(void)agent_height` / `(void)agent_radius` — only 1-voxel floor/headroom |
| `npc_pathfind_svo_astar.c` | 101-102 | `(void)agent_radius` / `(void)agent_height` — no clearance check |
| `npc_pathfind_shortcut.c` | — | inherited from A* |

No component validates that the agent physically fits through the computed path.

## Fix
1. `npc_svo_floodfill.c`: check `agent_height/voxel_size` voxels below for solid floor and above for empty headroom
2. `npc_pathfind_svo_astar.c`: expand walkability check to require empty voxels within `agent_radius/voxel_size` horizontally, and `agent_height/voxel_size` vertically above

## Acceptance
- [ ] Tall agent cannot path through low ceiling
- [ ] Wide agent cannot path through narrow corridor
- [ ] Existing tests still pass (agents larger than 1 voxel)
