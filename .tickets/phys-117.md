---
id: phys-117
status: open
deps: [phys-115, phys-116]
links: [phys-100]
created: 2026-02-06T05:20:00.000000000-08:00
type: task
priority: 1
---
# Step 1.17: Phase 1 Integration Test + Benchmark

**Parent Epic:** phys-100 (Phase 1: Complete Pipeline)

## Description

Comprehensive integration tests and benchmarks for complete Phase 1 pipeline.

## Files to create

- `tests/physics/phase1_integration_tests.c`
- `tests/physics/phase1_bench.c`

## Integration Test Cases

```c
// test_sphere_falls_on_floor
phys_world_t world;
phys_world_init(&world, &config);

// Static floor
pool_handle_t floor = phys_world_create_body(&world);
// Leave as static (inv_mass = 0)
phys_world_set_sphere_collider(&world, floor, 100.0f, (phys_vec3_t){0,0,0});

// Dynamic sphere
pool_handle_t ball = phys_world_create_body(&world);
phys_body_t *b = phys_world_get_body(&world, ball);
b->position = (phys_vec3_t){0, 10, 0};
phys_body_set_mass(b, 1.0f);
phys_body_set_sphere_inertia(b, 1.0f, 0.5f);
phys_world_set_sphere_collider(&world, ball, 0.5f, (phys_vec3_t){0,0,0});

// Run until ball rests
for (int i = 0; i < 300; ++i) {
    phys_world_tick(&world, NULL);
}

b = phys_world_get_body(&world, ball);
ASSERT_FLOAT_NEAR(b->position.y, 100.5f, 0.5f);  // resting on floor
ASSERT_FLOAT_NEAR(b->linear_vel.y, 0.0f, 0.1f);

// test_sphere_stack_stable
// Stack 5 spheres, run 300 ticks
// Verify top sphere hasn't fallen through

// test_two_spheres_elastic_collision
// Two spheres approaching, e=1
// Verify momentum conserved

// test_warmstart_improves_convergence
// Compare iteration count with/without warmstart

// test_sleep_wake_cycle
// Ball rests, another ball hits it
// Verify sleeping ball wakes

// test_network_round_trip_during_simulation
// Encode/decode mid-simulation
// Verify physics continues correctly
```

## Benchmark Cases

```c
// bench_100_spheres_30hz
phys_world_t world;
// Create 100 random spheres

uint64_t total_ns = 0;
for (int tick = 0; tick < 100; ++tick) {
    uint64_t start = platform_time_ns();
    phys_world_tick(&world, NULL);
    total_ns += platform_time_ns() - start;
}

float avg_ms = (total_ns / 100) / 1e6f;
printf("100 spheres avg: %.2f ms/tick\n", avg_ms);
ASSERT(avg_ms < 1.5f);

// bench_1000_spheres_30hz
// Target: < 15 ms/tick (single-threaded)

// bench_snapshot_encode_100
// Target: < 50 µs

// bench_snapshot_delta_100_10pct
// Target: < 20 µs
```

## Acceptance Criteria

- [ ] All functional tests pass
- [ ] Stacking is stable over 300+ frames
- [ ] Momentum conserved in collisions
- [ ] Warmstarting demonstrably helps
- [ ] Sleep/wake works correctly
- [ ] Network replication accurate
- [ ] Performance within targets

## Network Integration Checklist

- [ ] Full snapshot encode/decode round-trips correctly
- [ ] Delta compression works for changed bodies only
- [ ] Quantization error < 1mm position, < 0.1° rotation
- [ ] Snapshot size < 25 bytes/body full
- [ ] Performance within targets
