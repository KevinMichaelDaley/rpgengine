---
id: phys-011
status: closed
deps: [phys-005, phys-010]
links: [phys-000]
created: 2026-02-06T05:20:00.000000000-08:00
type: task
priority: 1
---
# Step 0.11: Island Structure

**Parent Epic:** phys-000 (Phase 0: Foundation Data Structures)

## Description

Define island structures and implement union-find algorithm for building
connected components from constraints. Islands enable parallel solving
with zero write contention.

## Files to create

- `include/ferrum/physics/island.h`
- `src/physics/solver/island.c`
- `tests/physics/island_tests.c`

## Structures

```c
typedef struct phys_island_t {
    uint32_t *body_indices;
    uint32_t body_count;
    uint32_t *constraint_indices;
    uint32_t constraint_count;
    bool sleeping;  // all bodies in island are sleeping
} phys_island_t;

typedef struct phys_island_list_t {
    phys_island_t *islands;
    uint32_t count;
    uint32_t capacity;
    
    // Union-find workspace (arena-allocated)
    uint32_t *parent;
    uint32_t *rank;
    uint32_t uf_size;
} phys_island_list_t;
```

## API

```c
void phys_island_list_init(phys_island_list_t *list, phys_frame_arena_t *arena, 
                            uint32_t max_bodies, uint32_t max_islands);
void phys_island_list_clear(phys_island_list_t *list);

// Build islands from constraints
void phys_island_list_build(phys_island_list_t *list, 
                             const phys_constraint_t *constraints, uint32_t constraint_count,
                             uint32_t body_count,
                             phys_frame_arena_t *arena);

// Union-find primitives
uint32_t phys_uf_find(phys_island_list_t *list, uint32_t x);
void phys_uf_union(phys_island_list_t *list, uint32_t x, uint32_t y);
```

## Acceptance Criteria

- [ ] Union-find correctly groups connected bodies
- [ ] Path compression for O(α(n)) find
- [ ] Union by rank for balanced trees
- [ ] Each island contains body and constraint indices
- [ ] Independent islands have no overlap
- [ ] Static bodies don't form their own islands (handled specially)

## Test Cases

```c
// test_union_find_single_component
phys_frame_arena_t arena;
phys_frame_arena_init(&arena, 1024 * 1024);

phys_island_list_t list;
phys_island_list_init(&list, &arena, 100, 50);

// 5 bodies all connected in a chain
phys_constraint_t constraints[4] = {
    {.body_a = 0, .body_b = 1},
    {.body_a = 1, .body_b = 2},
    {.body_a = 2, .body_b = 3},
    {.body_a = 3, .body_b = 4},
};

phys_island_list_build(&list, constraints, 4, 5, &arena);

ASSERT(list.count == 1);  // one island
ASSERT(list.islands[0].body_count == 5);
ASSERT(list.islands[0].constraint_count == 4);

phys_frame_arena_destroy(&arena);

// test_union_find_multiple_components
phys_frame_arena_init(&arena, 1024 * 1024);
phys_island_list_init(&list, &arena, 100, 50);

// Two separate chains: 0-1-2 and 3-4
phys_constraint_t constraints2[3] = {
    {.body_a = 0, .body_b = 1},
    {.body_a = 1, .body_b = 2},
    {.body_a = 3, .body_b = 4},
};

phys_island_list_build(&list, constraints2, 3, 5, &arena);

ASSERT(list.count == 2);  // two islands

// Find which island has 3 bodies
bool found_3 = false, found_2 = false;
for (uint32_t i = 0; i < list.count; ++i) {
    if (list.islands[i].body_count == 3) found_3 = true;
    if (list.islands[i].body_count == 2) found_2 = true;
}
ASSERT(found_3 && found_2);

phys_frame_arena_destroy(&arena);

// test_isolated_bodies
phys_frame_arena_init(&arena, 1024 * 1024);
phys_island_list_init(&list, &arena, 100, 50);

// No constraints, 3 bodies
phys_island_list_build(&list, NULL, 0, 3, &arena);

// Could be 3 single-body islands or 0 islands (isolated bodies skipped)
// Implementation-dependent; verify no crash
phys_frame_arena_destroy(&arena);

// test_path_compression
phys_frame_arena_init(&arena, 1024 * 1024);
phys_island_list_init(&list, &arena, 1000, 100);

// Long chain to test path compression
for (int i = 0; i < 100; ++i) {
    phys_uf_union(&list, i, i + 1);
}

// All should have same root
uint32_t root = phys_uf_find(&list, 0);
for (int i = 1; i <= 100; ++i) {
    ASSERT(phys_uf_find(&list, i) == root);
}

phys_frame_arena_destroy(&arena);
```
