---
id: phys-106
status: open
deps: [phys-105]
links: [phys-100]
created: 2026-02-06T05:20:00.000000000-08:00
type: task
priority: 1
---
# Step 1.6: Broadphase Stage (Stage 5)

**Parent Epic:** phys-100 (Phase 1: Complete Pipeline)

## Description

Implement Stage 5: Broadphase. Uses spatial grid to find AABB-overlapping
pairs. Output is a list of body index pairs to test in narrowphase.

## Files to create

- `include/ferrum/physics/broadphase.h`
- `src/physics/stages/broadphase.c`
- `tests/physics/broadphase_tests.c`

## Structures

```c
typedef struct phys_collision_pair_t {
    uint32_t body_a;
    uint32_t body_b;
} phys_collision_pair_t;
```

## API

```c
typedef struct phys_broadphase_args_t {
    const phys_aabb_t *aabbs;
    const phys_spatial_grid_t *grid;
    const phys_tier_lists_t *tier_lists;
    phys_collision_pair_t *pairs_out;
    uint32_t max_pairs;
    uint32_t *pair_count_out;
    phys_frame_arena_t *arena;
} phys_broadphase_args_t;

void phys_stage_broadphase(const phys_broadphase_args_t *args);
```

## Implementation

```c
void phys_stage_broadphase(const phys_broadphase_args_t *args) {
    uint32_t pair_count = 0;
    
    // For each active body, query grid and test AABB overlap
    for (int tier = PHYS_TIER_0_DIRECT; tier <= PHYS_TIER_4_BACKGROUND; ++tier) {
        const phys_tier_list_t *list = &args->tier_lists->tiers[tier];
        
        for (uint32_t i = 0; i < list->count; ++i) {
            uint32_t body_a = list->indices[i];
            const phys_aabb_t *aabb_a = &args->aabbs[body_a];
            
            uint32_t candidates[256];
            uint32_t count = phys_spatial_grid_query(args->grid, aabb_a, candidates, 256);
            
            for (uint32_t j = 0; j < count; ++j) {
                uint32_t body_b = candidates[j];
                
                // Avoid duplicates: only process if a < b
                if (body_a >= body_b) continue;
                
                // Precise AABB test
                if (phys_aabb_overlap(aabb_a, &args->aabbs[body_b])) {
                    if (pair_count < args->max_pairs) {
                        args->pairs_out[pair_count++] = (phys_collision_pair_t){body_a, body_b};
                    }
                }
            }
        }
    }
    
    *args->pair_count_out = pair_count;
}
```

## Acceptance Criteria

- [ ] All overlapping AABB pairs detected
- [ ] No duplicate pairs (a,b) and (b,a)
- [ ] No self-pairs (a,a)
- [ ] Pairs sorted (body_a < body_b)
- [ ] Static-static pairs excluded (future optimization)

## Test Cases

```c
// test_broadphase_finds_overlapping
// Two overlapping spheres → 1 pair
// Two separated spheres → 0 pairs

// test_broadphase_no_duplicates
// Three mutually overlapping spheres → 3 pairs (0-1, 0-2, 1-2)

// test_broadphase_no_self_pairs
// Single body → 0 pairs
```
