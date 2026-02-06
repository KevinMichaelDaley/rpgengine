---
id: phys-207
status: closed
deps: [phys-201, phys-202, phys-203, phys-204, phys-205, phys-206]
links: [phys-200]
created: 2026-02-06T11:09:00.000000000-08:00
type: task
priority: 1
---
# Step 2.7: Phase 2 Integration Test + Benchmark


**Parent Epic:** phys-200 (Phase 2: Box and Capsule Colliders)

## Description

End-to-end integration tests and benchmarks for all 6 primitive collision pairs.

## Files

- `tests/physics/phase2_integration_tests.c`
- `tests/physics/phase2_bench.c`

## Test Cases

```c
// test_box_stack_stable            (10 boxes, 1000 frames)
// test_capsule_rolls_on_floor
// test_mixed_primitive_pile        (sphere + box + capsule, 50 bodies)
// test_all_primitive_pairs_collide (6 pair types)
// test_compound_vs_primitive_collisions
// test_compound_animated_walk_cycle (bone-driven compound on floor)
```

## Benchmarks

```c
// bench_100_boxes_30hz               Target: < 1.0 ms
// bench_100_capsules_30hz            Target: < 1.0 ms
// bench_100_mixed_primitives         Target: < 1.0 ms
// bench_narrowphase_all_pair_types   Per-pair timing validation:
//   sphere-sphere ~0.3 µs, box-box ~1.5 µs, capsule-capsule ~1.0 µs
```

## Network Tests

```c
// test_snapshot_with_boxes_and_capsules
// test_delta_compression_mixed_types
```

## Acceptance Criteria

- [ ] All 6 primitive pair types produce correct contacts
- [ ] 10-box stack stable for 1000 frames
- [ ] Performance within per-pair timing targets
- [ ] Network snapshot round-trip works with all primitive types

