---
id: rpg-e54l
status: open
deps: [rpg-xhl7]
links: []
created: 2026-02-07T19:44:41Z
type: task
priority: 1
assignee: KMD
parent: rpg-m9nw
---
# Sparse stabilization: Pipeline integration and Baumgarte removal

Wire the sparse position projection into the physics tick pipeline (tick.c and tick_parallel.c). For TGS-tier constraints: remove Baumgarte bias from constraint_build, add sparse position projection as a post-solve step. Keep Baumgarte for XPBD-tier constraints unchanged. Update step_plan to account for the new stage timing.

## Acceptance Criteria

TGS-tier contacts use position projection instead of Baumgarte; XPBD-tier contacts unchanged; all existing tests pass; no regression in single-body or two-body scenarios

