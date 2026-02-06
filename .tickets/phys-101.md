---
id: phys-101
status: open
deps: [phys-013]
links: [phys-100]
created: 2026-02-06T05:20:00.000000000-08:00
type: task
priority: 1
---
# Step 1.1: Step Plan Stage (Stage 0)

**Parent Epic:** phys-100 (Phase 1: Complete Pipeline)

## Description

Implement Stage 0: Step Plan. Determines simulation parameters for this tick
based on world config and game state.

## Files to create

- `include/ferrum/physics/step_plan.h`
- `src/physics/stages/step_plan.c`
- `tests/physics/step_plan_tests.c`

## Structures

```c
typedef struct phys_game_state_t {
    phys_vec3_t *player_positions;
    uint32_t player_count;
    pool_handle_t *manipulated_bodies;
    uint32_t manipulated_count;
    // Future: camera position, manipulation flags, etc.
} phys_game_state_t;

typedef struct phys_tier_params_t {
    bool active;
    uint32_t substeps;
    uint32_t iterations;
    float friction_boost;
    float restitution_scale;
} phys_tier_params_t;

typedef struct phys_step_plan_t {
    uint32_t substeps;
    uint32_t solver_iterations;
    float dt;
    float substep_dt;
    phys_tier_params_t tier_params[PHYS_TIER_COUNT];
} phys_step_plan_t;
```

## API

```c
void phys_stage_step_plan(phys_step_plan_t *plan, 
                           const phys_world_t *world, 
                           const phys_game_state_t *game);
```

## Implementation

For Phase 1, use simple defaults:
- substeps = config.default_substeps
- iterations = config.default_solver_iterations
- dt = config.fixed_dt
- All tiers active with same parameters

Future phases will add per-tier fidelity.

## Acceptance Criteria

- [ ] Plan computed from world config
- [ ] substep_dt = dt / substeps
- [ ] Tier params initialized (all same for Phase 1)
- [ ] NULL game state handled gracefully

## Test Cases

```c
// test_step_plan_default
phys_world_t world;
phys_world_config_t cfg = phys_world_config_default();
cfg.default_substeps = 2;
cfg.default_solver_iterations = 8;
cfg.fixed_dt = 1.0f / 30.0f;
phys_world_init(&world, &cfg);

phys_step_plan_t plan;
phys_stage_step_plan(&plan, &world, NULL);

ASSERT(plan.substeps == 2);
ASSERT(plan.solver_iterations == 8);
ASSERT_FLOAT_NEAR(plan.dt, 1.0f / 30.0f, 0.0001f);
ASSERT_FLOAT_NEAR(plan.substep_dt, 1.0f / 60.0f, 0.0001f);

phys_world_destroy(&world);

// test_step_plan_tier_params
phys_stage_step_plan(&plan, &world, NULL);
for (int t = 0; t < PHYS_TIER_COUNT; ++t) {
    // Phase 1: all tiers have same config
    ASSERT(plan.tier_params[t].active == true);
}
```
