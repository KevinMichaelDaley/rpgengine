---
id: phys-114
status: closed
deps: [phys-113]
links: [phys-100]
created: 2026-02-06T05:20:00.000000000-08:00
type: task
priority: 1
---
# Step 1.14: Cache Commit + Events Stage (Stage 13)

**Parent Epic:** phys-100 (Phase 1: Complete Pipeline)

## Description

Implement Stage 13: Cache Commit + Events. Writes solved impulses back to
manifold cache for warmstarting and emits impact events for gameplay.

## Files to create

- `include/ferrum/physics/cache_commit.h`
- `src/physics/stages/cache_commit.c`
- `tests/physics/cache_commit_tests.c`

## Structures

```c
typedef struct phys_impact_event_t {
    uint32_t body_a;
    uint32_t body_b;
    phys_vec3_t point;
    phys_vec3_t normal;
    float impulse_magnitude;
} phys_impact_event_t;
```

## API

```c
typedef struct phys_cache_commit_args_t {
    const phys_manifold_t *manifolds;
    const phys_constraint_t *constraints;
    uint32_t constraint_count;
    phys_manifold_cache_t *cache;
    phys_impact_event_t *events_out;
    uint32_t *event_count_out;
    uint32_t max_events;
    float impact_threshold;
} phys_cache_commit_args_t;

void phys_stage_cache_commit(const phys_cache_commit_args_t *args);
```

## Implementation

```c
void phys_stage_cache_commit(const phys_cache_commit_args_t *args) {
    uint32_t event_count = 0;
    
    for (uint32_t i = 0; i < args->constraint_count; ++i) {
        const phys_constraint_t *c = &args->constraints[i];
        
        // Find manifold in cache
        phys_manifold_t *cached = phys_manifold_cache_find(
            args->cache, c->body_a, c->body_b);
        
        if (cached && c->point_idx < cached->point_count) {
            // Write back impulses
            cached->normal_impulse[c->point_idx] = c->rows[0].lambda;
            cached->tangent_impulse[c->point_idx][0] = c->rows[1].lambda;
            cached->tangent_impulse[c->point_idx][1] = c->rows[2].lambda;
        }
        
        // Emit impact event for significant impulses
        float impulse = fabsf(c->rows[0].lambda);
        if (impulse > args->impact_threshold && event_count < args->max_events) {
            const phys_manifold_t *m = &args->manifolds[c->manifold_idx];
            
            args->events_out[event_count++] = (phys_impact_event_t){
                .body_a = c->body_a,
                .body_b = c->body_b,
                .point = m->points[c->point_idx].point_world,
                .normal = m->points[c->point_idx].normal,
                .impulse_magnitude = impulse
            };
        }
    }
    
    *args->event_count_out = event_count;
}
```

## Acceptance Criteria

- [ ] Solved impulses written to cache
- [ ] Impact events emitted for high-impulse contacts
- [ ] Threshold filters out minor contacts
- [ ] Cache ready for next frame warmstart

## Test Cases

```c
// test_cache_commit_warmstart
// Solve, commit, verify cached impulses match

// test_cache_commit_impact_event
// High impulse → event emitted

// test_cache_commit_threshold
// Low impulse → no event
```
