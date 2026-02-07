---
id: phys-408
status: closed
deps: [phys-402, phys-403, phys-404, phys-405, phys-406, phys-407]
links: [phys-400]
created: 2026-02-06T11:09:00.000000000-08:00
type: task
priority: 1
---
# Step 4.8: Phase 4 Integration Test + Benchmark


**Parent Epic:** phys-400 (Phase 4: Tiered Simulation)

## Test Cases

```c
// test_tier_promotion_on_approach
// test_tier_demotion_on_distance
// test_hysteresis_prevents_flapping
// test_t0_stability_vs_t4
// test_occluded_t1_demotes_to_t3
// test_sphere_simplify_at_t3_contacts_valid
// test_planar_constraint_specialization
// test_sphere_constraint_specialization
// test_planar_sleep_propagation_deep_stack
// test_compound_collider_narrowphase_per_child
// test_compound_bone_driven_update
```

## Benchmarks

```c
// bench_5000_bodies_tiered               Target: < 3 ms/tick
// bench_8000_bodies_far_field_xpbd       Target: < 3 ms/tick (XPBD ceiling)
// bench_500_t4_background_props          Target: < 25 µs contribution
// bench_constraint_specialization_vs_generic
//   Target: ~0.32 µs/constraint vs ~0.50 µs generic
// bench_occlusion_demotion_20_bodies     Target: ~270 µs savings
// bench_sphere_simplify_narrowphase      Target: ~0.3 µs vs ~2.0 µs
// bench_compound_collider_10_children    Target: < 20 µs per compound
```

## Acceptance Criteria

- [ ] All optimization features working correctly together
- [ ] 5000 bodies tiered within 3ms budget
- [ ] Full optimization stack delivers expected speedups

