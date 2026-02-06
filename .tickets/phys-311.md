---
id: phys-311
status: open
deps: [phys-302, phys-303, phys-304, phys-305, phys-306, phys-307, phys-308, phys-309, phys-309b, phys-310]
links: [phys-300]
created: 2026-02-06T11:09:00.000000000-08:00
type: task
priority: 1
---
# Step 3.11: Phase 3 Integration Test + Benchmark


**Parent Epic:** phys-300 (Phase 3: Parallel Jobs)

## Test Cases

```c
// test_parallel_determinism    (same result single-threaded vs multi-threaded)
// test_parallel_no_data_races  (ASan/TSan clean under high contention)
```

## Benchmarks

```c
// bench_1000_bodies_4_threads         Target: < 1.5 ms/tick
// bench_3000_bodies_8_threads         Target: < 2.5 ms/tick
// bench_5000_bodies_8_threads         Target: < 3.0 ms/tick
// bench_scaling_1_to_8_threads        Expect ~3.5× speedup at 8 threads
// bench_snapshot_encode_parallel       Target: < 100 µs for 1000 bodies
```

## Acceptance Criteria

- [ ] Deterministic results (bit-exact vs single-threaded)
- [ ] TSan clean
- [ ] Performance scales with thread count

