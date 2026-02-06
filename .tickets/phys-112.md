---
id: phys-112
status: open
deps: [phys-111]
links: [phys-100]
created: 2026-02-06T11:09:00.000000000-08:00
type: task
priority: 1
---
# Step 1.12: Hybrid Solve Stage (Stage 11a/11b)


**Parent Epic:** phys-100 (Phase 1: Complete Pipeline)

## Description

Implement Stage 11: Hybrid Solve with two concurrent solvers:
- **Stage 11a: TGS** (Temporal Gauss-Seidel) for T0/T1 bodies (island-based, sequential per island)
- **Stage 11b: Jacobi XPBD** for T2–T4 bodies (parallel over bodies, no island decomposition)

Also implements solver transition logic for warm-start conversion when bodies
cross the TGS/XPBD boundary (T1↔T2).

## Files to create

- `include/ferrum/physics/tgs_solve.h`
- `include/ferrum/physics/xpbd_solve.h`
- `src/physics/solver/tgs_solve.c`
- `src/physics/solver/xpbd_solve.c`
- `src/physics/solver/solver_transition.c`
- `tests/physics/tgs_solve_tests.c`
- `tests/physics/xpbd_solve_tests.c`
- `tests/physics/solver_transition_tests.c`

## API

```c
typedef struct phys_velocity_t {
    phys_vec3_t linear;
    phys_vec3_t angular;
} phys_velocity_t;

// --- Stage 11a: TGS Solve (T0/T1, island-based) ---
typedef struct phys_tgs_solve_args_t {
    const phys_island_list_t *islands;
    phys_constraint_t *constraints;  // modified (lambda updated)
    const phys_body_t *bodies_in;
    phys_velocity_t *velocities_out;
    uint32_t body_count;
    uint32_t iterations;             // 20-24 for T0/T1
} phys_tgs_solve_args_t;

void phys_stage_tgs_solve(const phys_tgs_solve_args_t *args);

// --- Stage 11b: Jacobi XPBD Solve (T2–T4, per-body parallel) ---
typedef struct phys_xpbd_solve_args_t {
    phys_constraint_t *constraints;
    uint32_t constraint_count;
    const phys_body_t *bodies_in;
    phys_body_t *bodies_out;
    phys_velocity_t *velocities_out;
    uint32_t body_count;
    uint32_t iterations;             // 2–8 (T2:8, T3:4, T4:2)
    float omega;                     // Jacobi relaxation factor (0.5–0.8)
    float dt;
} phys_xpbd_solve_args_t;

void phys_stage_xpbd_solve(const phys_xpbd_solve_args_t *args);

// --- Solver transition (warm-start conversion) ---
void phys_solver_convert_tgs_to_xpbd(phys_constraint_t *c, float dt);
void phys_solver_convert_xpbd_to_tgs(phys_constraint_t *c, float dt);
```

## TGS Algorithm (T0/T1)

```
for each island:
    copy velocities from bodies_in
    for iter in 0..iterations:
        for each constraint row:
            Δλ = (bias - J·v) / effective_mass
            λ_new = clamp(λ + Δλ, λ_min, λ_max)
            apply velocity correction
    write final velocities to velocities_out
```

## Jacobi XPBD Algorithm (T2–T4)

```
for iter in 0..K:
    for each constraint (parallel, no order dependency):
        C = evaluate_constraint(positions)
        α̃ = compliance / dt²
        Δλ = (-C - α̃·λ) / (∇C^T M^-1 ∇C + α̃)
        accumulate position corrections per body
    for each body: position += ω * accumulated_Δx
velocity = (position_new - position_old) / dt
```

## Transition Logic

```
TGS → XPBD (body demoted T1 → T2): λ_xpbd = λ_impulse * dt
XPBD → TGS (body promoted T2 → T1): λ_impulse = clamp(λ_xpbd / dt, λ_min, λ_max)
```

## Test Cases

```c
// test_tgs_solve_simple_collision (momentum conservation)
// test_tgs_solve_static_floor (ball stops on floor)
// test_tgs_solve_stack_stability (3 spheres stacked, 100 iterations)
// test_xpbd_solve_unconditionally_stable (far-field explosion, no divergence)
// test_xpbd_solve_position_correction (penetrating bodies separate)
// test_tgs_to_xpbd_conversion_roundtrip
// test_xpbd_to_tgs_clamping (no energy injection)
// test_cross_tier_constraint_assignment
```

## Acceptance Criteria

- [ ] TGS converges for stable stacking (T0/T1 bodies)
- [ ] XPBD is unconditionally stable for far-field explosions
- [ ] Friction prevents sliding on resting contacts (both solvers)
- [ ] Restitution produces correct bounce velocity (TGS)
- [ ] Islands solved independently (TGS)
- [ ] XPBD parallelizes over bodies, not islands
- [ ] Transition TGS→XPBD preserves warm-start continuity
- [ ] Transition XPBD→TGS clamps to avoid energy injection

