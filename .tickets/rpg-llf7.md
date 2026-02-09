---
id: rpg-llf7
status: open
deps: []
links: []
created: 2026-02-09T04:33:51Z
type: task
priority: 2
assignee: KMD
parent: rpg-d3sq
tags: [physics, perf]
---
# Wire XPBD solve for T3/T4

Implement XPBD solve path for far-field tiers (T3/T4):
- Decide scratch buffers + integration hookup
- Update tick.c + tick_parallel.c stage orchestration
- Add tests + bench case

