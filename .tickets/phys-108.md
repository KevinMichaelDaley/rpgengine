---
id: phys-108
status: closed
deps: [phys-107]
links: [phys-100]
created: 2026-02-06T05:20:00.000000000-08:00
type: task
priority: 1
---
# Step 1.8: Manifold Build + Cache Merge (Stage 7)

**Parent Epic:** phys-100 (Phase 1: Complete Pipeline)

## Description

Implement Stage 7: Manifold Build. Merges new contact candidates with
cached manifolds to enable warmstarting. Reduces to ≤4 best contact points.

## Files to create

- `include/ferrum/physics/manifold_build.h`
- `src/physics/stages/manifold_build.c`
- `tests/physics/manifold_build_tests.c`

## API

```c
typedef struct phys_manifold_build_args_t {
    const phys_contact_candidate_t *candidates;
    uint32_t candidate_count;
    phys_manifold_cache_t *cache;
    phys_manifold_t *manifolds_out;
    uint32_t *manifold_count_out;
    uint32_t max_manifolds;
    uint64_t tick;
} phys_manifold_build_args_t;

void phys_stage_manifold_build(const phys_manifold_build_args_t *args);
```

## Implementation

```c
void phys_stage_manifold_build(const phys_manifold_build_args_t *args) {
    uint32_t manifold_count = 0;
    
    for (uint32_t i = 0; i < args->candidate_count; ++i) {
        const phys_contact_candidate_t *cand = &args->candidates[i];
        
        // Get or create cached manifold
        phys_manifold_t *cached = phys_manifold_cache_get_or_create(
            args->cache, cand->body_a, cand->body_b, args->tick);
        
        // Save old impulses for warmstart matching
        float old_normal[PHYS_MAX_MANIFOLD_POINTS];
        float old_tangent[PHYS_MAX_MANIFOLD_POINTS][2];
        uint32_t old_features[PHYS_MAX_MANIFOLD_POINTS];
        uint8_t old_count = cached->point_count;
        
        for (uint8_t j = 0; j < old_count; ++j) {
            old_normal[j] = cached->normal_impulse[j];
            old_tangent[j][0] = cached->tangent_impulse[j][0];
            old_tangent[j][1] = cached->tangent_impulse[j][1];
            old_features[j] = cached->points[j].feature_id;
        }
        
        // Clear and add new points
        phys_manifold_clear(cached);
        cached->body_a = cand->body_a;
        cached->body_b = cand->body_b;
        
        for (uint8_t j = 0; j < cand->contact_count; ++j) {
            phys_manifold_add_point(cached, &cand->contacts[j]);
        }
        
        // Match new contacts to old by feature ID, restore impulses
        for (uint8_t j = 0; j < cached->point_count; ++j) {
            uint32_t feat = cached->points[j].feature_id;
            for (uint8_t k = 0; k < old_count; ++k) {
                if (old_features[k] == feat) {
                    cached->normal_impulse[j] = old_normal[k];
                    cached->tangent_impulse[j][0] = old_tangent[k][0];
                    cached->tangent_impulse[j][1] = old_tangent[k][1];
                    break;
                }
            }
        }
        
        // Copy to output
        if (manifold_count < args->max_manifolds) {
            args->manifolds_out[manifold_count++] = *cached;
        }
    }
    
    *args->manifold_count_out = manifold_count;
}
```

## Acceptance Criteria

- [ ] Manifolds created from candidates
- [ ] Cached impulses matched by feature ID
- [ ] New contacts get zero warmstart
- [ ] Point count reduced to ≤4
- [ ] Cache touched with current tick

## Test Cases

```c
// test_manifold_build_new_contact
// First contact between pair: warmstart = 0

// test_manifold_build_warmstart_match
// Second frame with same feature ID: warmstart preserved

// test_manifold_build_reduce_points
// >4 contacts reduced to 4
```
