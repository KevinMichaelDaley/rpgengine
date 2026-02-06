---
id: phys-012
status: open
deps: [phys-002, phys-003, phys-004, phys-005, phys-006, phys-007, phys-008, phys-009, phys-010, phys-011]
links: [phys-000]
created: 2026-02-06T05:20:00.000000000-08:00
type: task
priority: 1
---
# Step 0.12: Physics World Container

**Parent Epic:** phys-000 (Phase 0: Foundation Data Structures)

## Description

Create the top-level physics world that owns all pools, caches, and
configuration. This is the main interface for creating/destroying bodies
and running simulation.

## Files to create

- `include/ferrum/physics/world.h`
- `src/physics/world/world.c`
- `tests/physics/world_tests.c`

## Structures

```c
typedef struct phys_world_config_t {
    uint32_t max_bodies;
    uint32_t max_colliders;
    uint32_t manifold_cache_size;
    size_t frame_arena_size;
    float fixed_dt;
    phys_vec3_t gravity;
    uint32_t default_substeps;
    uint32_t default_solver_iterations;
    float baumgarte;
    float slop;
    float sleep_threshold_linear;
    float sleep_threshold_angular;
    uint32_t sleep_delay_frames;
} phys_world_config_t;

typedef struct phys_world_t {
    phys_world_config_t config;
    
    // Pools
    phys_body_pool_t body_pool;
    pool_t collider_pool;
    pool_t sphere_pool;
    pool_t box_pool;
    pool_t capsule_pool;
    
    // Per-body arrays (1:1 with body pool)
    phys_collider_t *colliders;
    phys_aabb_t *aabbs;
    
    // Persistent cache
    phys_manifold_cache_t manifold_cache;
    
    // Per-tick arena
    phys_frame_arena_t frame_arena;
    
    // Impact events (populated by tick, consumed by gameplay)
    phys_impact_event_t *impact_events;
    uint32_t impact_event_count;
    uint32_t impact_event_capacity;
    float impact_threshold;
    
    // Tick state
    uint64_t tick_count;
} phys_world_t;

// Default config
phys_world_config_t phys_world_config_default(void);
```

## API

```c
int phys_world_init(phys_world_t *world, const phys_world_config_t *config);
void phys_world_destroy(phys_world_t *world);

// Body management
pool_handle_t phys_world_create_body(phys_world_t *world);
void phys_world_destroy_body(phys_world_t *world, pool_handle_t h);
phys_body_t *phys_world_get_body(phys_world_t *world, pool_handle_t h);
const phys_body_t *phys_world_get_body_const(const phys_world_t *world, pool_handle_t h);
bool phys_world_body_exists(const phys_world_t *world, pool_handle_t h);

// Collider management
void phys_world_set_sphere_collider(phys_world_t *world, pool_handle_t body, float radius, phys_vec3_t offset);
void phys_world_set_box_collider(phys_world_t *world, pool_handle_t body, phys_vec3_t half_extents, phys_vec3_t offset, phys_quat_t rotation);
void phys_world_set_capsule_collider(phys_world_t *world, pool_handle_t body, float radius, float half_height, phys_vec3_t offset, phys_quat_t rotation);
const phys_collider_t *phys_world_get_collider(const phys_world_t *world, pool_handle_t body);

// AABB access (computed by tick)
const phys_aabb_t *phys_world_get_aabb(const phys_world_t *world, pool_handle_t body);

// Impact events (see phys-119 for full API)
const phys_impact_event_t *phys_world_get_impact_events(const phys_world_t *world, uint32_t *out_count);
void phys_world_clear_impact_events(phys_world_t *world);
void phys_world_set_impact_threshold(phys_world_t *world, float threshold);

// Stats
uint32_t phys_world_body_count(const phys_world_t *world);
uint64_t phys_world_tick_count(const phys_world_t *world);
```

## Acceptance Criteria

- [ ] World initializes all subsystems with config
- [ ] Body creation allocates from pool
- [ ] Collider setters allocate shape and link to body
- [ ] AABB array sized for max bodies
- [ ] Destroy frees all memory cleanly
- [ ] Default config has reasonable values

## Test Cases

```c
// test_world_init_destroy
phys_world_config_t cfg = phys_world_config_default();
cfg.max_bodies = 1000;

phys_world_t world;
ASSERT(phys_world_init(&world, &cfg) == 0);
ASSERT(phys_world_body_count(&world) == 0);

phys_world_destroy(&world);
// Verify no leaks with valgrind/ASan

// test_world_create_body
phys_world_init(&world, &cfg);

pool_handle_t h = phys_world_create_body(&world);
ASSERT(phys_world_body_exists(&world, h));
ASSERT(phys_world_body_count(&world) == 1);

phys_body_t *b = phys_world_get_body(&world, h);
ASSERT(b != NULL);
b->position = (phys_vec3_t){1, 2, 3};
phys_body_set_mass(b, 1.0f);

phys_world_destroy(&world);

// test_world_set_collider
phys_world_init(&world, &cfg);

pool_handle_t h1 = phys_world_create_body(&world);
phys_world_set_sphere_collider(&world, h1, 0.5f, (phys_vec3_t){0,0,0});

const phys_collider_t *c = phys_world_get_collider(&world, h1);
ASSERT(c != NULL);
ASSERT(c->type == PHYS_SHAPE_SPHERE);

phys_world_destroy(&world);

// test_world_multiple_collider_types
phys_world_init(&world, &cfg);

pool_handle_t sphere = phys_world_create_body(&world);
pool_handle_t box = phys_world_create_body(&world);
pool_handle_t capsule = phys_world_create_body(&world);

phys_world_set_sphere_collider(&world, sphere, 1.0f, (phys_vec3_t){0,0,0});
phys_world_set_box_collider(&world, box, (phys_vec3_t){1,2,3}, (phys_vec3_t){0,0,0}, PHYS_QUAT_IDENTITY);
phys_world_set_capsule_collider(&world, capsule, 0.5f, 1.0f, (phys_vec3_t){0,0,0}, PHYS_QUAT_IDENTITY);

ASSERT(phys_world_get_collider(&world, sphere)->type == PHYS_SHAPE_SPHERE);
ASSERT(phys_world_get_collider(&world, box)->type == PHYS_SHAPE_BOX);
ASSERT(phys_world_get_collider(&world, capsule)->type == PHYS_SHAPE_CAPSULE);

phys_world_destroy(&world);

// test_world_destroy_body
phys_world_init(&world, &cfg);

pool_handle_t h2 = phys_world_create_body(&world);
ASSERT(phys_world_body_exists(&world, h2));

phys_world_destroy_body(&world, h2);
ASSERT(!phys_world_body_exists(&world, h2));
ASSERT(phys_world_body_count(&world) == 0);

phys_world_destroy(&world);
```
