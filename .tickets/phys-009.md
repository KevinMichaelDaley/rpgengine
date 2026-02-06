---
id: phys-009
status: closed
deps: [phys-008]
links: [phys-000]
created: 2026-02-06T05:20:00.000000000-08:00
type: task
priority: 1
---
# Step 0.9: Manifold Cache (Persistent)

**Parent Epic:** phys-000 (Phase 0: Foundation Data Structures)

## Description

Implement persistent manifold cache that survives across frames. Enables
warmstarting by storing accumulated impulses from previous solver iterations.

## Files to create

- `include/ferrum/physics/manifold_cache.h`
- `src/physics/collision/manifold_cache.c`
- `tests/physics/manifold_cache_tests.c`

## Structures

```c
typedef struct phys_manifold_cache_entry_t {
    uint64_t pair_key;           // body_a << 32 | body_b (sorted)
    phys_manifold_t manifold;
    uint32_t last_used_tick;     // for expiry
    uint32_t flags;
} phys_manifold_cache_entry_t;

typedef struct phys_manifold_cache_t {
    phys_manifold_cache_entry_t *entries;
    uint32_t capacity;
    uint32_t count;
    // Hash table for O(1) lookup
    uint32_t *hash_table;        // indices into entries, or INVALID
    uint32_t hash_size;          // power of 2
    uint32_t hash_mask;
} phys_manifold_cache_t;

#define PHYS_CACHE_INVALID_INDEX 0xFFFFFFFF
```

## API

```c
int phys_manifold_cache_init(phys_manifold_cache_t *cache, uint32_t capacity);
void phys_manifold_cache_destroy(phys_manifold_cache_t *cache);

// Returns existing manifold or NULL
phys_manifold_t *phys_manifold_cache_find(phys_manifold_cache_t *cache, uint32_t body_a, uint32_t body_b);

// Returns new or existing manifold
phys_manifold_t *phys_manifold_cache_get_or_create(phys_manifold_cache_t *cache, uint32_t body_a, uint32_t body_b, uint64_t tick);

// Remove entries not used in last N ticks
void phys_manifold_cache_expire(phys_manifold_cache_t *cache, uint64_t current_tick, uint32_t max_age);

// Update last_used_tick for an entry
void phys_manifold_cache_touch(phys_manifold_cache_t *cache, uint32_t body_a, uint32_t body_b, uint64_t tick);

// Stats
uint32_t phys_manifold_cache_count(const phys_manifold_cache_t *cache);
```

## Acceptance Criteria

- [ ] O(1) average lookup by body pair
- [ ] Body pairs normalized (smaller ID first)
- [ ] Warmstart data persists across frames
- [ ] Old entries expire after K frames without contact
- [ ] Hash collision handling works correctly

## Test Cases

```c
// test_cache_create_and_find
phys_manifold_cache_t cache;
phys_manifold_cache_init(&cache, 1000);

phys_manifold_t *m = phys_manifold_cache_get_or_create(&cache, 5, 10, 1);
ASSERT(m != NULL);
ASSERT(m->body_a == 5);
ASSERT(m->body_b == 10);

// Find should return same
phys_manifold_t *found = phys_manifold_cache_find(&cache, 5, 10);
ASSERT(found == m);

// Reverse order should also find
found = phys_manifold_cache_find(&cache, 10, 5);
ASSERT(found == m);

phys_manifold_cache_destroy(&cache);

// test_cache_not_found
phys_manifold_cache_init(&cache, 1000);
phys_manifold_t *not_found = phys_manifold_cache_find(&cache, 1, 2);
ASSERT(not_found == NULL);
phys_manifold_cache_destroy(&cache);

// test_cache_warmstart_persists
phys_manifold_cache_init(&cache, 1000);

phys_manifold_t *m1 = phys_manifold_cache_get_or_create(&cache, 1, 2, 1);
m1->normal_impulse[0] = 5.0f;

// Simulate next frame lookup
phys_manifold_t *m2 = phys_manifold_cache_find(&cache, 1, 2);
ASSERT(m2->normal_impulse[0] == 5.0f);

phys_manifold_cache_destroy(&cache);

// test_cache_expiry
phys_manifold_cache_init(&cache, 1000);

phys_manifold_cache_get_or_create(&cache, 1, 2, 1);
phys_manifold_cache_get_or_create(&cache, 3, 4, 1);
phys_manifold_cache_get_or_create(&cache, 5, 6, 5);  // created later

ASSERT(phys_manifold_cache_count(&cache) == 3);

// Expire entries older than 3 ticks from tick 10
phys_manifold_cache_expire(&cache, 10, 3);

// 1,2 and 3,4 should be expired (last used at tick 1)
// 5,6 should remain (last used at tick 5)
ASSERT(phys_manifold_cache_find(&cache, 1, 2) == NULL);
ASSERT(phys_manifold_cache_find(&cache, 5, 6) != NULL);

phys_manifold_cache_destroy(&cache);

// test_cache_hash_collision
phys_manifold_cache_init(&cache, 1000);

// Insert many pairs to force collisions
for (uint32_t i = 0; i < 100; ++i) {
    phys_manifold_cache_get_or_create(&cache, i, i + 1000, 1);
}

// All should be findable
for (uint32_t i = 0; i < 100; ++i) {
    ASSERT(phys_manifold_cache_find(&cache, i, i + 1000) != NULL);
}

phys_manifold_cache_destroy(&cache);
```
