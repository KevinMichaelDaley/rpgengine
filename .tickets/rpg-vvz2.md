---
id: rpg-vvz2
status: in_progress
deps: []
links: []
created: 2026-02-09T04:33:51Z
type: task
priority: 2
assignee: KMD
parent: rpg-d3sq
tags: [physics, perf]
---
# Add greedy island graph coloring

For islands above a size threshold:
- Build constraint adjacency graph
- Apply greedy lowest-degree-first coloring
- Solve constraints per color batch (parallel where possible)
- Add tests + bench case

