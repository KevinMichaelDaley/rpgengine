---
id: rpg-lhrq
status: open
deps: []
links: []
created: 2026-02-15T08:45:10Z
type: task
priority: 2
assignee: KMD
---
# Half-space collision primitive (infinite plane)

Add PHYS_SHAPE_HALFSPACE as a new collision primitive. A half-space is an infinite plane defined by normal+distance where everything behind it is penetrating. Benefits: zero BVH overhead, exact ground planes, no edge artifacts. Implement sphere/capsule/box vs halfspace narrowphase tests. Add to all 3 dispatch paths. Half-spaces are always solid by definition.

