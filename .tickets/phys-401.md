---
id: phys-401
status: open
deps: [phys-311]
links: [phys-400]
created: 2026-02-06T11:09:00.000000000-08:00
type: task
priority: 1
---
# Step 4.1: Distance-Based Tier Classification


**Parent Epic:** phys-400 (Phase 4: Tiered Simulation)

## Description

Classify bodies into tiers by distance to nearest player:
- T0: < 5m (direct manipulation)
- T1: same room / few seconds' walk / visible through window (near interactive)
- T2: < 50m (visible but not immediate)
- T3: < 200m (world-shaping, or occluded nearby)
- T4: > 200m (background)
- T5: sleeping

Hysteresis: bodies keep their tier for K frames to prevent flapping.

## Files

- `src/physics/stages/tier_classify.c` (extend existing)
- `tests/physics/tier_classify_tests.c` (extend existing)

## Acceptance Criteria

- [ ] Distance-based classification correct
- [ ] Hysteresis prevents tier flapping
- [ ] T1 definition uses room/visibility, not just distance

