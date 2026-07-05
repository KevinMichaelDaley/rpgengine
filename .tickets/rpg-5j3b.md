---
id: rpg-5j3b
status: closed
deps: [rpg-a3dm]
links: []
created: 2026-07-05T06:25:47Z
type: task
priority: 0
assignee: KMD
parent: rpg-t6ia
tags: [procgen]
---
# srd-004: srd_energy.cpp -- Room SDF energy element

**DO NOT CREATE STUBS OR PARTIAL IMPLEMENTATIONS; NEVER SKIP FEATURES OR DEFER STEPS!**



Room box SDF: sdf=max(|x-cx|-hx,|y-cy|-hy,|z-cz|-hz). Occupancy=sigmoid(-sdf/T). Energy=sum(occ-target)^2. RED-phase: tests/procgen/srd/srd_energy_tests.cpp

## Acceptance Criteria

Energy<1e-3 at target; gradient correct sign on all 6 params

