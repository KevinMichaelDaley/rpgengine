---
id: phys-604
status: open
deps: [phys-601, phys-602, phys-603]
links: [phys-600]
created: 2026-02-06T11:09:00.000000000-08:00
type: task
priority: 1
---
# Step 6.4: Phase 6 Integration Test + Benchmark


**Parent Epic:** phys-600 (Phase 6: Static BVH)

## Benchmarks

```c
// bench_dynamic_vs_static_10000_static_bodies
// Compare grid-only vs BVH+grid
```

## Acceptance Criteria

- [ ] BVH+grid faster than grid-only for high static body count
- [ ] Correct collision with static geometry

