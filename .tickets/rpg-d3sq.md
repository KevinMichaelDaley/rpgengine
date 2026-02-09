---
id: rpg-d3sq
status: open
deps: []
links: []
created: 2026-02-09T04:33:51Z
type: epic
priority: 2
assignee: KMD
tags: [physics, perf]
---
# Physics optimizations: XPBD T2/T3/T4 + island coloring

Implement two major physics performance upgrades:

1) Wire XPBD solve for far-field tiers (T2/T3/T4)
2) Add greedy lowest-degree-first graph coloring for large islands to enable parallel constraint processing / island splitting

Context (current repo state): src/physics/world/tick*.c currently runs TGS-only; XPBD modules exist but are not wired. The solver also uses split impulse (pseudo_velocities[]) so there is no separate position projection stage.


## Design

Design sketch:

A) XPBD for T2/T3/T4
- Add a Stage 11b XPBD path that operates on bodies in tiers T2/T3/T4.
- Keep TGS for near-field tiers (T0/T1).
- Data flow: constraint build produces constraint rows tagged with solver mode and tier; XPBD accumulates positional corrections into per-body scratch; integrate consumes corrections (likely via pseudo_velocities[] or a dedicated xpbd_delta_pos[] buffer).
- Ensure determinism constraints and bounded memory via frame_arena allocations.

B) Island coloring for large islands
- For islands larger than a threshold (by bodies or constraints), compute a constraint graph and apply greedy lowest-degree-first coloring.
- Process constraints by color batches (each color can be solved in parallel without conflicts) to reduce worst-case TGS island serialization.
- Threshold and ordering should be configurable (world config).

Tracy:
- Add zones for XPBD solve and coloring passes following Phys.* naming.


## Acceptance Criteria

Acceptance criteria:
- XPBD solve is executed for T2/T3/T4 bodies and produces stable stacks/contacts at distance without destabilizing near-field.
- Large-island coloring reduces wall-clock time in synthetic dense scenarios (documented with a bench + Tracy capture).
- Unit/regression tests cover:
  - XPBD path activation by tier
  - coloring correctness (no two adjacent constraints share a color) and stability
- No per-frame malloc/free on hot path; uses frame_arena/pools.


