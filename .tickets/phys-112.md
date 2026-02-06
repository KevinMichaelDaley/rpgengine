---
id: phys-112
status: open
deps: [phys-111]
links: [phys-100]
created: 2026-02-06T05:20:00.000000000-08:00
type: task
priority: 1
---
# Step 1.12: TGS Solve Stage (Stage 11)

**Parent Epic:** phys-100 (Phase 1: Complete Pipeline)

## Description

Implement Stage 11: TGS (Temporal Gauss-Seidel) Solve. Iteratively solves
constraints to compute velocity corrections.

## Files to create

- `include/ferrum/physics/tgs_solve.h`
- `src/physics/solver/tgs_solve.c`
- `tests/physics/tgs_solve_tests.c`

## Structures

```c
typedef struct phys_velocity_t {
    phys_vec3_t linear;
    phys_vec3_t angular;
} phys_velocity_t;
```

## API

```c
typedef struct phys_tgs_solve_args_t {
    const phys_island_list_t *islands;
    phys_constraint_t *constraints;  // modified (lambda updated)
    const phys_body_t *bodies;
    phys_velocity_t *velocities_out;
    uint32_t body_count;
    uint32_t iterations;
} phys_tgs_solve_args_t;

void phys_stage_tgs_solve(const phys_tgs_solve_args_t *args);

// Solve single constraint row, returns delta lambda
float phys_solve_row(phys_jacobian_row_t *row, 
                      phys_velocity_t *vel_a, phys_velocity_t *vel_b,
                      float inv_mass_a, const phys_vec3_t *inv_I_a,
                      float inv_mass_b, const phys_vec3_t *inv_I_b);
```

## TGS Algorithm

```c
void phys_stage_tgs_solve(const phys_tgs_solve_args_t *args) {
    // Initialize velocities from bodies
    for (uint32_t i = 0; i < args->body_count; ++i) {
        args->velocities_out[i].linear = args->bodies[i].linear_vel;
        args->velocities_out[i].angular = args->bodies[i].angular_vel;
    }
    
    // Solve each island
    for (uint32_t i = 0; i < args->islands->count; ++i) {
        const phys_island_t *island = &args->islands->islands[i];
        
        for (uint32_t iter = 0; iter < args->iterations; ++iter) {
            for (uint32_t c = 0; c < island->constraint_count; ++c) {
                uint32_t c_idx = island->constraint_indices[c];
                phys_constraint_t *constraint = &args->constraints[c_idx];
                
                const phys_body_t *body_a = &args->bodies[constraint->body_a];
                const phys_body_t *body_b = &args->bodies[constraint->body_b];
                phys_velocity_t *vel_a = &args->velocities_out[constraint->body_a];
                phys_velocity_t *vel_b = &args->velocities_out[constraint->body_b];
                
                // Solve each row
                for (uint8_t r = 0; r < constraint->row_count; ++r) {
                    phys_solve_row(&constraint->rows[r], vel_a, vel_b,
                        body_a->inv_mass, &body_a->inv_inertia_diag,
                        body_b->inv_mass, &body_b->inv_inertia_diag);
                }
            }
        }
    }
}

float phys_solve_row(phys_jacobian_row_t *row, ...) {
    // Compute relative velocity along constraint
    float Jv = vec3_dot(row->J_va, vel_a->linear)
             + vec3_dot(row->J_wa, vel_a->angular)
             + vec3_dot(row->J_vb, vel_b->linear)
             + vec3_dot(row->J_wb, vel_b->angular);
    
    // Compute impulse
    float delta_lambda = (row->bias - Jv) * row->effective_mass;
    
    // Clamp accumulated impulse
    float old_lambda = row->lambda;
    row->lambda = fmaxf(row->lambda_min, fminf(row->lambda_max, row->lambda + delta_lambda));
    delta_lambda = row->lambda - old_lambda;
    
    // Apply impulse
    vel_a->linear = vec3_add(vel_a->linear, vec3_scale(row->J_va, inv_mass_a * delta_lambda));
    vel_a->angular = vec3_add(vel_a->angular, vec3_mul(row->J_wa, 
        vec3_scale(*inv_I_a, delta_lambda)));
    // ... same for body B with negated Jacobians
    
    return delta_lambda;
}
```

## Acceptance Criteria

- [ ] Constraints solved iteratively
- [ ] Lambda accumulated and clamped
- [ ] Velocities updated correctly
- [ ] Converges for stable stacking
- [ ] Friction prevents sliding

## Test Cases

```c
// test_tgs_solve_simple_collision
// Two spheres colliding head-on: momentum conserved

// test_tgs_solve_static_floor
// Ball falling on static floor: stops correctly

// test_tgs_solve_stack_stability
// Stack of 3 spheres: doesn't collapse over 100 iterations
```
