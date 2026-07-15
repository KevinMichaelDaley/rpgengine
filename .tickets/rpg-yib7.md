---
id: rpg-yib7
status: open
deps: [rpg-ats5, rpg-nvw0]
links: []
created: 2026-07-13T05:23:51Z
type: task
priority: 2
assignee: KMD
parent: rpg-fsvq
---
# Cascade selection + shadow lookup in material shader + integration test

Per-fragment cascade selection and shadow lookup wired through the forward+ pass; integration test verifying static geometry shadows from the cached map and dynamic objects from the per-frame map, co-sampled.

## Design

Core renderer. Depends on the co-sampled term + forward+ pass.

