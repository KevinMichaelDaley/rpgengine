---
id: phys-110
status: closed
deps: [phys-109]
links: [phys-100]
created: 2026-02-06T05:20:00.000000000-08:00
type: task
priority: 1
---
# Step 1.10: Constraint Build Stage (Stage 9)

**Parent Epic:** phys-100 (Phase 1: Complete Pipeline)

## Description

Implement Stage 9: Constraint Build. Generates Jacobian constraint rows
from manifolds with stabilization hints applied.

## Files to create

- `include/ferrum/physics/constraint_build.h`
- `src/physics/stages/constraint_build.c`
- `tests/physics/constraint_build_tests.c`

## API

```c
typedef struct phys_constraint_build_args_t {
    const phys_manifold_t *manifolds;
    const phys_stab_hint_t *hints;
    uint32_t manifold_count;
    const phys_body_t *bodies;
    phys_constraint_t *constraints_out;
    uint32_t *constraint_count_out;
    uint32_t max_constraints;
    float dt;
    float baumgarte;
    float slop;
} phys_constraint_build_args_t;

void phys_stage_constraint_build(const phys_constraint_build_args_t *args);
```

## Implementation

```c
void phys_stage_constraint_build(const phys_constraint_build_args_t *args) {
    uint32_t c_count = 0;
    
    for (uint32_t m = 0; m < args->manifold_count; ++m) {
        const phys_manifold_t *manifold = &args->manifolds[m];
        const phys_stab_hint_t *hint = &args->hints[m];
        const phys_body_t *body_a = &args->bodies[manifold->body_a];
        const phys_body_t *body_b = &args->bodies[manifold->body_b];
        
        float friction = manifold->friction * hint->friction_scale;
        float restitution = manifold->restitution * hint->restitution_scale;
        
        for (uint8_t p = 0; p < manifold->point_count; ++p) {
            if (c_count >= args->max_constraints) break;
            
            phys_constraint_t *c = &args->constraints_out[c_count++];
            
            phys_constraint_build_contact(c, body_a, body_b,
                &manifold->points[p], friction, restitution,
                args->dt, args->baumgarte, args->slop);
            
            c->body_a = manifold->body_a;
            c->body_b = manifold->body_b;
            c->manifold_idx = m;
            c->point_idx = p;
            
            // Load warmstart from manifold
            c->rows[0].lambda = manifold->normal_impulse[p];
            c->rows[1].lambda = manifold->tangent_impulse[p][0];
            c->rows[2].lambda = manifold->tangent_impulse[p][1];
        }
    }
    
    *args->constraint_count_out = c_count;
}
```

## Acceptance Criteria

- [ ] 3 rows per contact (normal + 2 friction)
- [ ] Stabilization hints applied to friction/restitution
- [ ] Warmstart impulses loaded from manifold
- [ ] Baumgarte bias computed correctly
- [ ] Body indices stored for solver

## Test Cases

```c
// test_constraint_build_from_manifold
// Single contact → 1 constraint with 3 rows

// test_constraint_build_warmstart
// Manifold with stored impulses → constraints warmstarted

// test_constraint_build_stabilization_applied
// Resting contact → friction boosted by 3x
```
