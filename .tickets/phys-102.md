---
id: phys-102
status: open
deps: [phys-013]
links: [phys-100]
created: 2026-02-06T05:20:00.000000000-08:00
type: task
priority: 1
---
# Step 1.2: Tier Classification Stage (Stage 1)

**Parent Epic:** phys-100 (Phase 1: Complete Pipeline)

## Description

Implement Stage 1: Base Tier Classification. Assigns each body to a tier (T0-T5)
based on game state. For Phase 1, all active bodies go to T0.

## Files to create

- `include/ferrum/physics/tier_classify.h`
- `src/physics/stages/tier_classify.c`
- `tests/physics/tier_classify_tests.c`

## API

```c
typedef struct phys_tier_classify_args_t {
    const phys_body_t *bodies;
    uint32_t body_count;
    const phys_game_state_t *game;
    phys_tier_lists_t *tier_lists_out;
    phys_frame_arena_t *arena;
} phys_tier_classify_args_t;

void phys_stage_tier_classify(const phys_tier_classify_args_t *args);
```

## Implementation (Phase 1 - Simple)

```c
void phys_stage_tier_classify(const phys_tier_classify_args_t *args) {
    phys_tier_lists_clear_all(args->tier_lists_out);
    phys_tier_lists_init(args->tier_lists_out, args->arena, args->body_count);
    
    for (uint32_t i = 0; i < args->body_count; ++i) {
        const phys_body_t *body = &args->bodies[i];
        
        if (phys_body_is_sleeping(body)) {
            phys_tier_list_add(&args->tier_lists_out->tiers[PHYS_TIER_5_SLEEPING], i);
        } else if (phys_body_is_static(body)) {
            // Static bodies don't go in tier lists (always available)
            continue;
        } else {
            // Phase 1: all dynamic bodies → T0
            phys_tier_list_add(&args->tier_lists_out->tiers[PHYS_TIER_0_DIRECT], i);
        }
    }
}
```

## Acceptance Criteria

- [ ] Sleeping bodies go to T5
- [ ] Static bodies excluded from tier lists
- [ ] Dynamic bodies go to T0 (Phase 1)
- [ ] Tier lists are arena-allocated

## Test Cases

```c
// test_classify_dynamic_to_t0
phys_body_t bodies[3];
phys_body_init(&bodies[0]); phys_body_set_mass(&bodies[0], 1.0f);
phys_body_init(&bodies[1]); phys_body_set_mass(&bodies[1], 1.0f);
phys_body_init(&bodies[2]); phys_body_set_mass(&bodies[2], 1.0f);

phys_frame_arena_t arena;
phys_frame_arena_init(&arena, 1024 * 1024);

phys_tier_lists_t lists;
phys_tier_classify_args_t args = {
    .bodies = bodies,
    .body_count = 3,
    .game = NULL,
    .tier_lists_out = &lists,
    .arena = &arena
};

phys_stage_tier_classify(&args);

ASSERT(lists.tiers[PHYS_TIER_0_DIRECT].count == 3);
ASSERT(lists.tiers[PHYS_TIER_5_SLEEPING].count == 0);

phys_frame_arena_destroy(&arena);

// test_classify_sleeping_to_t5
phys_body_set_sleeping(&bodies[1], true);
phys_frame_arena_reset(&arena);
phys_stage_tier_classify(&args);

ASSERT(lists.tiers[PHYS_TIER_0_DIRECT].count == 2);
ASSERT(lists.tiers[PHYS_TIER_5_SLEEPING].count == 1);

// test_classify_static_excluded
phys_body_init(&bodies[2]);  // reset to static (inv_mass = 0)
phys_frame_arena_reset(&arena);
phys_stage_tier_classify(&args);

// Static body excluded, sleeping still in T5, one dynamic in T0
ASSERT(lists.tiers[PHYS_TIER_0_DIRECT].count == 1);
```
