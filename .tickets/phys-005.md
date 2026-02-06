---
id: phys-005
status: closed
deps: [phys-002]
links: [phys-000]
created: 2026-02-06T05:20:00.000000000-08:00
type: task
priority: 1
---
# Step 0.5: Body Pool and Frame Arena

**Parent Epic:** phys-000 (Phase 0: Foundation Data Structures)

## Description

Create physics-specific wrappers around engine pool and arena allocators.
Body pool provides double-buffered access (bodies_curr/bodies_next).
Frame arena provides per-tick transient allocations.

## Files to create

- `include/ferrum/physics/phys_pool.h`
- `src/physics/memory/phys_pool.c`
- `src/physics/memory/phys_arena.c`
- `tests/physics/phys_pool_tests.c`

## API

```c
// Body pool (wraps engine pool_t with physics-specific interface)
typedef struct phys_body_pool_t {
    pool_t pool;
    phys_body_t *bodies_curr;
    phys_body_t *bodies_next;
    uint32_t capacity;
} phys_body_pool_t;

int phys_body_pool_init(phys_body_pool_t *pool, uint32_t capacity);
void phys_body_pool_destroy(phys_body_pool_t *pool);
pool_handle_t phys_body_pool_alloc(phys_body_pool_t *pool);
void phys_body_pool_free(phys_body_pool_t *pool, pool_handle_t h);
phys_body_t *phys_body_pool_get_curr(phys_body_pool_t *pool, pool_handle_t h);
phys_body_t *phys_body_pool_get_next(phys_body_pool_t *pool, pool_handle_t h);
void phys_body_pool_swap_buffers(phys_body_pool_t *pool);
uint32_t phys_body_pool_active_count(const phys_body_pool_t *pool);
bool phys_body_pool_handle_valid(const phys_body_pool_t *pool, pool_handle_t h);

// Frame arena (wraps engine arena_t)
typedef struct phys_frame_arena_t {
    arena_t arena;
    size_t capacity;
} phys_frame_arena_t;

int phys_frame_arena_init(phys_frame_arena_t *arena, size_t size);
void phys_frame_arena_destroy(phys_frame_arena_t *arena);
void *phys_frame_arena_alloc(phys_frame_arena_t *arena, size_t size, size_t align);
void phys_frame_arena_reset(phys_frame_arena_t *arena);
size_t phys_frame_arena_used(const phys_frame_arena_t *arena);
size_t phys_frame_arena_remaining(const phys_frame_arena_t *arena);
```

## Acceptance Criteria

- [ ] Pool provides double-buffered body access
- [ ] Buffer swap is O(1) pointer exchange
- [ ] Generation handles prevent use-after-free
- [ ] Arena allocations 16-byte aligned by default
- [ ] Arena reset is O(1)
- [ ] Arena tracks usage correctly

## Test Cases

```c
// test_body_pool_alloc_free
phys_body_pool_t pool;
phys_body_pool_init(&pool, 100);

pool_handle_t h = phys_body_pool_alloc(&pool);
ASSERT(phys_body_pool_handle_valid(&pool, h));
ASSERT(phys_body_pool_active_count(&pool) == 1);

phys_body_t *b = phys_body_pool_get_curr(&pool, h);
ASSERT(b != NULL);
b->position = (phys_vec3_t){1, 2, 3};

phys_body_pool_free(&pool, h);
ASSERT(!phys_body_pool_handle_valid(&pool, h));
ASSERT(phys_body_pool_active_count(&pool) == 0);

phys_body_pool_destroy(&pool);

// test_body_pool_double_buffer
phys_body_pool_init(&pool, 100);
pool_handle_t h1 = phys_body_pool_alloc(&pool);

phys_body_t *curr = phys_body_pool_get_curr(&pool, h1);
phys_body_t *next = phys_body_pool_get_next(&pool, h1);
ASSERT(curr != next);  // different buffers

curr->position = (phys_vec3_t){1, 0, 0};
next->position = (phys_vec3_t){2, 0, 0};

phys_body_pool_swap_buffers(&pool);

// After swap, what was 'next' is now 'curr'
curr = phys_body_pool_get_curr(&pool, h1);
ASSERT_VEC3_EQ(curr->position, (phys_vec3_t){2, 0, 0});

phys_body_pool_destroy(&pool);

// test_frame_arena_alloc_reset
phys_frame_arena_t arena;
phys_frame_arena_init(&arena, 1024 * 1024);

void *p1 = phys_frame_arena_alloc(&arena, 1000, 16);
ASSERT(p1 != NULL);
ASSERT(((uintptr_t)p1 & 15) == 0);  // 16-byte aligned

size_t used = phys_frame_arena_used(&arena);
ASSERT(used >= 1000);

void *p2 = phys_frame_arena_alloc(&arena, 500, 16);
ASSERT(p2 != NULL);
ASSERT(phys_frame_arena_used(&arena) > used);

phys_frame_arena_reset(&arena);
ASSERT(phys_frame_arena_used(&arena) == 0);

phys_frame_arena_destroy(&arena);

// test_frame_arena_exhaustion
phys_frame_arena_init(&arena, 1024);
void *p = phys_frame_arena_alloc(&arena, 2048, 16);
ASSERT(p == NULL);  // too large
phys_frame_arena_destroy(&arena);
```
