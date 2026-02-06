---
id: phys-407
status: open
deps: [phys-401]
links: [phys-400]
created: 2026-02-06T11:09:00.000000000-08:00
type: task
priority: 1
---
# Step 4.7: Sphere Simplification at Distance


**Parent Epic:** phys-400 (Phase 4: Tiered Simulation)

## Description

At asset load, compute bounding-sphere ratio = circumradius / inradius.
If ratio < 1.3, set `collider.sphere_simplify = 1`.

Good candidates: rocks, skulls, potions, rubble chunks (~1.1–1.2).
Bad: pipes (~2.0), planks, bottles (~1.5+).

## Runtime behavior

- T2+: both bodies with flag → sphere-sphere narrowphase (~0.3 µs vs ~2.0 µs)
- T3+: any single body with flag → use bounding sphere for that body
- T4: sphere collider preferred unconditionally for small objects

## Files

- `src/physics/collision/narrowphase.c` (extend dispatch)
- `tests/physics/sphere_simplify_tests.c`

## Test Cases

```c
// test_sphere_ratio_computation
// test_sphere_simplify_flag_set_for_near_sphere
// test_sphere_simplify_flag_clear_for_elongated
// test_narrowphase_sphere_override_at_t3
// test_narrowphase_both_sphere_override_at_t2
// test_sphere_simplified_contact_reasonable_accuracy
```

## Acceptance Criteria

- [ ] Ratio correctly computed from shape geometry
- [ ] Flag set at init, immutable at runtime
- [ ] Narrowphase dispatch respects tier + flag combination
- [ ] Contact positions within ~5% of full-fidelity for near-spherical bodies

