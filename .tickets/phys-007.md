---
id: phys-007
status: open
deps: [phys-004, phys-005]
links: [phys-000]
created: 2026-02-06T05:20:00.000000000-08:00
type: task
priority: 1
---
# Step 0.7: Spatial Hash Grid

**Parent Epic:** phys-000 (Phase 0: Foundation Data Structures)

## Description

Implement spatial hash grid for broadphase collision detection. Bodies are
inserted based on their AABB, and queries return all bodies overlapping a
given AABB.

## Files to create

- `include/ferrum/physics/spatial_grid.h`
- `src/physics/broadphase/spatial_grid.c`
- `tests/physics/spatial_grid_tests.c`

## Structures

```c
typedef struct phys_grid_cell_t {
    uint32_t *body_indices;
    uint32_t count;
    uint32_t capacity;
} phys_grid_cell_t;

typedef struct phys_spatial_grid_t {
    phys_grid_cell_t *cells;   // hash table (power of 2 size)
    uint32_t cell_count;       // power of 2
    uint32_t cell_mask;        // cell_count - 1
    float cell_size;
    float inv_cell_size;
    phys_frame_arena_t *arena; // for cell allocations
} phys_spatial_grid_t;
```

## API

```c
void phys_spatial_grid_init(phys_spatial_grid_t *grid, uint32_t cell_count, float cell_size, phys_frame_arena_t *arena);
void phys_spatial_grid_clear(phys_spatial_grid_t *grid);
void phys_spatial_grid_insert(phys_spatial_grid_t *grid, uint32_t body_index, const phys_aabb_t *aabb);
uint32_t phys_spatial_grid_query(const phys_spatial_grid_t *grid, const phys_aabb_t *aabb, 
                                  uint32_t *out_indices, uint32_t max_results);

// Hash function
static inline uint32_t phys_grid_hash(int32_t x, int32_t y, int32_t z) {
    // Morton-like hash or simple hash
    return ((uint32_t)x * 73856093u) ^ ((uint32_t)y * 19349663u) ^ ((uint32_t)z * 83492791u);
}
```

## Acceptance Criteria

- [ ] Bodies inserted into correct cells based on AABB
- [ ] Large AABBs inserted into multiple cells
- [ ] Query returns all bodies overlapping query AABB
- [ ] No heap allocations during insert/query (uses arena)
- [ ] Clear resets all cells to empty
- [ ] Hash collisions handled correctly

## Test Cases

```c
// test_grid_insert_single_cell
phys_frame_arena_t arena;
phys_frame_arena_init(&arena, 1024 * 1024);

phys_spatial_grid_t grid;
phys_spatial_grid_init(&grid, 1024, 10.0f, &arena);  // 10m cells

// Small AABB fits in one cell
phys_aabb_t aabb = {{5, 5, 5}, {6, 6, 6}};
phys_spatial_grid_insert(&grid, 42, &aabb);

uint32_t results[100];
uint32_t count = phys_spatial_grid_query(&grid, &aabb, results, 100);
ASSERT(count == 1);
ASSERT(results[0] == 42);

// test_grid_insert_multiple_cells
phys_spatial_grid_clear(&grid);

// Large AABB spans multiple cells
phys_aabb_t large = {{0, 0, 0}, {25, 25, 25}};  // spans 3x3x3 cells
phys_spatial_grid_insert(&grid, 7, &large);

// Query one cell should find it
phys_aabb_t query = {{5, 5, 5}, {6, 6, 6}};
count = phys_spatial_grid_query(&grid, &query, results, 100);
ASSERT(count >= 1);
// Body 7 should be in results

// test_grid_query_no_results
phys_aabb_t far = {{1000, 1000, 1000}, {1001, 1001, 1001}};
count = phys_spatial_grid_query(&grid, &far, results, 100);
ASSERT(count == 0);

// test_grid_multiple_bodies_same_cell
phys_spatial_grid_clear(&grid);

phys_aabb_t a1 = {{0, 0, 0}, {1, 1, 1}};
phys_aabb_t a2 = {{2, 2, 2}, {3, 3, 3}};
phys_spatial_grid_insert(&grid, 1, &a1);
phys_spatial_grid_insert(&grid, 2, &a2);

// Both in same cell, query should return both
phys_aabb_t cell_query = {{0, 0, 0}, {9, 9, 9}};
count = phys_spatial_grid_query(&grid, &cell_query, results, 100);
ASSERT(count == 2);

phys_frame_arena_destroy(&arena);
```
