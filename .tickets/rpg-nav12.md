---
id: rpg-nav12
status: closed
deps: [rpg-nav02]
links: []
created: 2026-05-03T20:00:00Z
type: bug
priority: 2
assignee: KMD
parent: rpg-nav01
tags: [navigation, bug, svo, floodfill, queue, overflow]
---
# npc_svo_floodfill Queue Silently Overflows at 65536 Entries

`npc_svo_floodfill_walkable` in `src/npc/nav/npc_svo_floodfill.c:98-99,118` caps the BFS queue at 65536 entries:
```c
if (queue_cap > 256 * 256) queue_cap = 256 * 256;
```
When the queue fills, `PUSH` silently drops new voxels (line 118: `if (qtail < queue_cap)`). For a max-depth-8 grid (256³ = 16M voxels), large open areas can exceed this cap, causing the floodfill to stop expanding prematurely — returning an artificially low `marked` count with no error indication.

## Fix
Option 1: Increase the cap to `cells * cells * cells / 2` (half the grid, worst case for a checkerboard pattern).
Option 2: Use a ring buffer or growable queue.
Option 3: Return partial result flag when queue overflowed.

## Acceptance
- [ ] Floodfill covers large open areas without silent truncation
- [ ] Queue overflow produces a warning or partial-result indicator
- [ ] Memory usage remains bounded for safety-critical contexts
