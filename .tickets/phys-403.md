---
id: phys-403
status: closed
deps: [phys-402]
links: [phys-400]
created: 2026-02-06T11:09:00.000000000-08:00
type: task
priority: 1
---
# Step 4.3: Solver Transition Logic (TGS ↔ XPBD)


**Parent Epic:** phys-400 (Phase 4: Tiered Simulation)

## Description

When a body's tier changes across the TGS/XPBD boundary (T1↔T2), cached
warm-start data must be converted.

**TGS → XPBD (demotion):** `λ_xpbd = λ_impulse * dt`
**XPBD → TGS (promotion):** `λ_impulse = clamp(λ_xpbd / dt, λ_min, λ_max)`

Clamping on promotion is essential to avoid energy injection.

## Files

- `src/physics/solver/solver_transition.c`
- `tests/physics/solver_transition_tests.c`

## Test Cases

```c
// test_tgs_to_xpbd_conversion_roundtrip
// test_xpbd_to_tgs_clamping
// test_cross_tier_constraint_assignment
// test_no_energy_injection_on_promotion
```

## Acceptance Criteria

- [ ] Round-trip conversion preserves continuity
- [ ] Clamping prevents energy injection on XPBD→TGS
- [ ] Transition is seamless (no visual pop)

