---
id: rpg-9f38
status: closed
deps: [rpg-5j3b, rpg-1fw9, rpg-3551, rpg-giey, rpg-4mwa]
links: []
created: 2026-07-05T06:51:26Z
type: task
priority: 0
assignee: KMD
parent: rpg-q5eq
tags: [procgen]
---
# srd-021: Milestone 2 smoke — energy elements integration

DO NOT CREATE STUBS OR PARTIAL IMPLEMENTATIONS; NEVER SKIP FEATURES OR DEFER STEPS!

Integration test after room SDF (srd-004), corridor SDF (srd-005), stair/overlap energy (srd-006), sampler (srd-007), and anneal (srd-008) are complete. Build a full dungeon scene: 12+ rooms of varying types across 2 floors, 4+ corridors connecting them, 1+ stair pair between floors. Verify: each room energy decreases when room is at its correct position, overlap energy is zero when rooms are separated, corridor energy decreases when endpoints touch room edges, stair energy decreases as steps fill vertical gap, gradient has correct sign for every element type.

RED-phase: tests/procgen/srd/srd_m2_energy_smoke.cpp — build 12 rooms + 4 corridors + 2 stairs across 2 floors; verify every energy element's value and gradient sign individually; verify composite energy decreases monotonically during gradient descent on all parameters simultaneously.

## Acceptance Criteria

Room energy < 0.01 at correct position for all 12 rooms; overlap energy < 0.01 for all room pairs; corridor energy < 0.01 when all endpoints touch their target rooms; stair energy < 0.01 when steps fill vertical gap; gradient descent reduces total composite energy by >90% within 100 iterations; no NaN or infinite values in any gradient

