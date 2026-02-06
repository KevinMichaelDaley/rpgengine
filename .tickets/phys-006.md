---
id: phys-006
status: closed
deps: [phys-005]
links: [phys-000]
created: 2026-02-06T05:20:00.000000000-08:00
type: task
priority: 1
---
# Step 0.6: Tier List Structure

**Parent Epic:** phys-000 (Phase 0: Foundation Data Structures)

## Description

Define tier list structures for T0-T5 simulation tiers. Tier lists are packed
index arrays pointing into the shared body pool, arena-allocated each frame.

## Files to create

- `include/ferrum/physics/tier_list.h`
- `src/physics/tier/tier_list.c`
- `tests/physics/tier_list_tests.c`

## Structures

```c
#define PHYS_TIER_COUNT 6  // T0-T5

typedef enum phys_tier_t {
    PHYS_TIER_0_DIRECT = 0,    // Direct manipulation
    PHYS_TIER_1_NEAR,          // Near interactive
    PHYS_TIER_2_VISIBLE,       // Visible / hazardous
    PHYS_TIER_3_WORLD,         // World-shaping
    PHYS_TIER_4_BACKGROUND,    // Background dynamic
    PHYS_TIER_5_SLEEPING,      // Sleeping / dormant
} phys_tier_t;

typedef struct phys_tier_list_t {
    uint32_t *indices;   // arena-allocated
    uint32_t count;
    uint32_t capacity;
} phys_tier_list_t;

typedef struct phys_tier_lists_t {
    phys_tier_list_t tiers[PHYS_TIER_COUNT];
} phys_tier_lists_t;
```

## API

```c
void phys_tier_lists_init(phys_tier_lists_t *lists, phys_frame_arena_t *arena, uint32_t max_bodies);
void phys_tier_list_add(phys_tier_list_t *list, uint32_t body_index);
void phys_tier_list_clear(phys_tier_list_t *list);
void phys_tier_lists_clear_all(phys_tier_lists_t *lists);
uint32_t phys_tier_lists_total_active(const phys_tier_lists_t *lists);  // T0-T4 only
```

## Acceptance Criteria

- [ ] 6 tier lists (T0-T5) available
- [ ] Lists are arena-allocated (no malloc during tick)
- [ ] Add is O(1) amortized
- [ ] Clear is O(1)
- [ ] Total active count excludes T5 (sleeping)

## Test Cases

```c
// test_tier_list_init_from_arena
phys_frame_arena_t arena;
phys_frame_arena_init(&arena, 1024 * 1024);

phys_tier_lists_t lists;
phys_tier_lists_init(&lists, &arena, 1000);

for (int t = 0; t < PHYS_TIER_COUNT; ++t) {
    ASSERT(lists.tiers[t].indices != NULL);
    ASSERT(lists.tiers[t].count == 0);
    ASSERT(lists.tiers[t].capacity >= 1000);
}

phys_frame_arena_destroy(&arena);

// test_tier_list_add
phys_tier_list_add(&lists.tiers[PHYS_TIER_0_DIRECT], 5);
phys_tier_list_add(&lists.tiers[PHYS_TIER_0_DIRECT], 10);
phys_tier_list_add(&lists.tiers[PHYS_TIER_1_NEAR], 7);

ASSERT(lists.tiers[PHYS_TIER_0_DIRECT].count == 2);
ASSERT(lists.tiers[PHYS_TIER_1_NEAR].count == 1);
ASSERT(lists.tiers[PHYS_TIER_0_DIRECT].indices[0] == 5);
ASSERT(lists.tiers[PHYS_TIER_0_DIRECT].indices[1] == 10);

// test_tier_list_clear
phys_tier_list_clear(&lists.tiers[PHYS_TIER_0_DIRECT]);
ASSERT(lists.tiers[PHYS_TIER_0_DIRECT].count == 0);

// test_total_active_excludes_sleeping
phys_tier_list_add(&lists.tiers[PHYS_TIER_0_DIRECT], 1);
phys_tier_list_add(&lists.tiers[PHYS_TIER_5_SLEEPING], 2);
phys_tier_list_add(&lists.tiers[PHYS_TIER_5_SLEEPING], 3);

ASSERT(phys_tier_lists_total_active(&lists) == 2);  // only T0 + T1
```
