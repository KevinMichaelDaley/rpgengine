---
id: phys-109
status: open
deps: [phys-108]
links: [phys-100]
created: 2026-02-06T05:20:00.000000000-08:00
type: task
priority: 1
---
# Step 1.9: Stabilization Hints Stage (Stage 8)

**Parent Epic:** phys-100 (Phase 1: Complete Pipeline)

## Description

Implement Stage 8: Stabilization Hints. Classifies contacts as resting/sliding/
separating and generates hints to improve solver stability.

## Files to create

- `include/ferrum/physics/stabilization.h`
- `src/physics/stages/stabilization.c`
- `tests/physics/stabilization_tests.c`

## Structures

```c
typedef struct phys_stab_hint_t {
    float friction_scale;     // multiplier on base friction
    float restitution_scale;  // multiplier (0 to suppress bounce)
} phys_stab_hint_t;
```

## API

```c
typedef struct phys_stabilization_args_t {
    const phys_manifold_t *manifolds;
    uint32_t manifold_count;
    const phys_body_t *bodies;
    const phys_tier_lists_t *tier_lists;
    phys_stab_hint_t *hints_out;
    float resting_velocity_threshold;
} phys_stabilization_args_t;

void phys_stage_stabilization(const phys_stabilization_args_t *args);
```

## Implementation

```c
void phys_stage_stabilization(const phys_stabilization_args_t *args) {
    for (uint32_t i = 0; i < args->manifold_count; ++i) {
        const phys_manifold_t *m = &args->manifolds[i];
        const phys_body_t *a = &args->bodies[m->body_a];
        const phys_body_t *b = &args->bodies[m->body_b];
        
        // Compute relative velocity at first contact point
        phys_vec3_t r_a = vec3_sub(m->points[0].point_world, a->position);
        phys_vec3_t r_b = vec3_sub(m->points[0].point_world, b->position);
        
        phys_vec3_t v_a = vec3_add(a->linear_vel, vec3_cross(a->angular_vel, r_a));
        phys_vec3_t v_b = vec3_add(b->linear_vel, vec3_cross(b->angular_vel, r_b));
        phys_vec3_t v_rel = vec3_sub(v_a, v_b);
        
        float v_n = vec3_dot(v_rel, m->points[0].normal);
        float v_t_sq = vec3_length_sq(v_rel) - v_n * v_n;
        
        phys_stab_hint_t *hint = &args->hints_out[i];
        
        if (fabsf(v_n) < args->resting_velocity_threshold && 
            v_t_sq < args->resting_velocity_threshold * args->resting_velocity_threshold) {
            // Resting contact: boost friction, suppress restitution
            hint->friction_scale = 3.0f;
            hint->restitution_scale = 0.0f;
        } else if (v_n > 0) {
            // Separating: normal response
            hint->friction_scale = 1.0f;
            hint->restitution_scale = 1.0f;
        } else {
            // Approaching: normal response
            hint->friction_scale = 1.0f;
            hint->restitution_scale = 1.0f;
        }
    }
}
```

## Acceptance Criteria

- [ ] Resting contacts get friction boost (~3x)
- [ ] Resting contacts get restitution suppression (→0)
- [ ] Fast contacts keep normal parameters
- [ ] Velocity threshold configurable

## Test Cases

```c
// test_stabilization_resting
// Bodies at rest → friction_scale = 3, restitution_scale = 0

// test_stabilization_approaching
// Fast collision → friction_scale = 1, restitution_scale = 1

// test_stabilization_separating
// Bodies separating → friction_scale = 1, restitution_scale = 1
```
