---
id: rpg-9cvi
status: open
deps: [rpg-e54l, rpg-1bul]
links: []
created: 2026-02-07T19:44:41Z
type: task
priority: 1
assignee: KMD
parent: rpg-m9nw
---
# Sparse stabilization: Demo validation (before/after comparison)

Run the box stack demo (rpg-1bul) with Baumgarte stabilization, record stacking behavior (max stable stack height, jitter amplitude, energy drift). Then switch to sparse position projection, run again, and compare. Document results in ref/. This task validates the entire sparse stabilization epic.

## Acceptance Criteria

Side-by-side comparison documented; sparse projection produces taller stable stacks than Baumgarte; no visible jitter in 20+ box stacks with uneven masses; physics budget not exceeded

