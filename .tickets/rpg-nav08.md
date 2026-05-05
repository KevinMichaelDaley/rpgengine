---
id: rpg-nav08
status: closed
deps: [rpg-nav04]
links: [rpg-nav04]
created: 2026-05-03T20:00:00Z
type: bug
priority: 1
assignee: KMD
parent: rpg-nav01
tags: [navigation, bug, shortcut, los, pathfinding]
---
# npc_pathfind_shortcut Ignores SVO and Blockers — No LOS Validation

`npc_pathfind_shortcut` in `src/npc/nav/npc_pathfind_shortcut.c:21` completely discards the SVO grid and dynamic blocker parameters:
```c
(void)svo; (void)blockers; (void)blocker_count;
```
The function performs only a geometric collinearity test (angle threshold). Shortcuts can — and will — cut straight through solid walls, pillars, and obstacles, producing invalid navigation paths.

## Root Cause
The function was implemented as a collinearity-only waypoint reducer. Line-of-sight validation requires walking the SVO tree from the anchor to each candidate waypoint, checking every intermediate voxel for SOLID or blockers.

## Fix
Add a `line_of_sight_clear()` helper that traces through voxels between two points using the SVO tree (DDA/bresenham 3D). Keep waypoints only if LOS is clear. Blockers are checked via `npc_svo_voxel_blocked` at each step.

## Acceptance
- [ ] Shortcut reduction never removes waypoints needed to go around solid geometry
- [ ] Dynamic blocker between two waypoints causes the intermediate waypoint to be retained
- [ ] Straight empty corridor waypoints are still collapsed
