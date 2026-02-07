---
id: rpg-1bul
status: closed
deps: [demo-001]
links: []
created: 2026-02-07T19:43:55Z
type: task
priority: 1
assignee: KMD
---
# Demo: Box Stack Stress Test Spawner

Replace the current distant random-body spawner with a box stack stress test spawner. Spawn stacks of 8-50 boxes with varying sizes (0.3-1.5m half-extents) and masses (200-2000 kg/m³ density). After construction, apply a random impulse to a random box. Stacks within 30m of origin, spaced 5m+, new stack every 3-5 seconds. Tests stacking stability for upcoming sparse stabilization.

## Acceptance Criteria

Stacks of 8-50 boxes spawn periodically; boxes have varying sizes/masses; random impulse applied after construction; stacks clustered within 30m; stacks can collide when toppled; runs 5+ minutes with 10+ stacks

