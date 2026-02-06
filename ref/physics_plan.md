# Physics Engine Implementation Plan

## Philosophy

This plan builds the **complete physics pipeline from day one**. We do not
implement simplified versions and iterate—we implement the full architecture
for basic shape colliders (sphere, box, capsule) with all 14 pipeline stages,
then extend with modular features (mesh colliders, joints, fracture).

**Core principles:**
- Every phase delivers the complete pipeline at that scope
- Network replication is tested and benchmarked at each phase
- No corners cut on the core loop—stabilization, tier lists, islands, TGS all present from Phase 1
- Extensions are purely additive—they plug into existing stages

---

## Pipeline Overview (All 14 Stages)

Every phase implements this complete dataflow:

```
═══════════════════════════════════════════════════════════════════════════════
PHYSICS TICK (30 Hz) - COMPLETE DATAFLOW
═══════════════════════════════════════════════════════════════════════════════

                    ┌─────────────────────────────────────────────────────────┐
                    │                    TICK START                           │
                    │  reset phys_frame_arena                                 │
                    │  bodies_curr = last frame's bodies_next                 │
                    └─────────────────────────────────────────────────────────┘
                                              │
                                              ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ STAGE 0: STEP PLAN                                                   [SYNC] │
├─────────────────────────────────────────────────────────────────────────────┤
│ read:  game_state (player pos, camera, manipulation flags)                  │
│ write: step_plan_t (substep counts, iteration counts, budgets per tier)     │
└─────────────────────────────────────────────────────────────────────────────┘
                                              │
                                              ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ STAGE 1: BASE TIER CLASSIFICATION                                  [PARALLEL]│
├─────────────────────────────────────────────────────────────────────────────┤
│ read:  bodies_curr (positions), player_positions[], manipulation_flags[]    │
│ write: tier_lists[0..5] (arena-allocated index arrays)                      │
└─────────────────────────────────────────────────────────────────────────────┘
                                              │
                                              ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ STAGE 2: SPATIAL INDEX UPDATE                                      [PARALLEL]│
├─────────────────────────────────────────────────────────────────────────────┤
│ read:  bodies_curr (positions), colliders (for AABB computation)            │
│ write: aabbs[], spatial_grid (hash grid cells)                              │
└─────────────────────────────────────────────────────────────────────────────┘
                                              │
                                              ▼
                    ╔═════════════════════════════════════════════════════════╗
                    ║         SUBSTEP LOOP (repeat N times per tick)          ║
                    ╚═════════════════════════════════════════════════════════╝
                                              │
                                              ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ STAGE 3: HALO CLOSURE                                              [PARALLEL]│
├─────────────────────────────────────────────────────────────────────────────┤
│ read:  tier_lists[T0], bodies_curr (velocities), spatial_grid              │
│ write: tier_lists[T0..T1] (expanded with promoted neighbors)                │
└─────────────────────────────────────────────────────────────────────────────┘
                                              │
                                              ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ STAGE 4: AABB UPDATE (ACTIVE SET)                                  [PARALLEL]│
├─────────────────────────────────────────────────────────────────────────────┤
│ read:  tier_lists[T0..T4], bodies_curr, colliders                           │
│ write: aabbs[] (only for active bodies)                                     │
└─────────────────────────────────────────────────────────────────────────────┘
                                              │
                                              ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ STAGE 5: BROADPHASE                                                [PARALLEL]│
├─────────────────────────────────────────────────────────────────────────────┤
│ read:  aabbs[], spatial_grid, static_bvh                                    │
│ write: collision_pairs[] (arena-allocated pair array)                       │
└─────────────────────────────────────────────────────────────────────────────┘
                                              │
                                              ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ STAGE 6: NARROWPHASE                                               [PARALLEL]│
├─────────────────────────────────────────────────────────────────────────────┤
│ read:  collision_pairs[], bodies_curr, colliders, shape_pools              │
│ write: contact_candidates[] (arena-allocated)                               │
└─────────────────────────────────────────────────────────────────────────────┘
                                              │
                                              ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ STAGE 7: MANIFOLD BUILD + CACHE MERGE                              [PARALLEL]│
├─────────────────────────────────────────────────────────────────────────────┤
│ read:  contact_candidates[], manifold_cache (persistent)                    │
│ write: manifolds[] (arena-allocated), manifold_cache (updated)              │
└─────────────────────────────────────────────────────────────────────────────┘
                                              │
                                              ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ STAGE 8: STABILIZATION HINTS                                       [PARALLEL]│
├─────────────────────────────────────────────────────────────────────────────┤
│ read:  manifolds[], bodies_curr (velocities), tier_lists                    │
│ write: stab_hints[] (per-manifold hints: friction boost, restitution mod)   │
└─────────────────────────────────────────────────────────────────────────────┘
                                              │
                                              ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ STAGE 9: CONSTRAINT BUILD                                          [PARALLEL]│
├─────────────────────────────────────────────────────────────────────────────┤
│ read:  manifolds[], stab_hints[], bodies_curr (mass, inertia)               │
│ write: constraints[] (arena-allocated Jacobian rows)                        │
└─────────────────────────────────────────────────────────────────────────────┘
                                              │
                                              ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ STAGE 10: ISLAND BUILD                                                [SYNC] │
├─────────────────────────────────────────────────────────────────────────────┤
│ read:  constraints[] (body pairs)                                           │
│ write: islands[] (list of constraint/body index ranges)                     │
└─────────────────────────────────────────────────────────────────────────────┘
                                              │
                                              ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ STAGE 11: TGS SOLVE                                                [PARALLEL]│
├─────────────────────────────────────────────────────────────────────────────┤
│ read:  islands[], constraints[], bodies_curr                                │
│ write: velocities_solved[]                                                  │
└─────────────────────────────────────────────────────────────────────────────┘
                                              │
                                              ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ STAGE 12: INTEGRATE + SLEEP                                        [PARALLEL]│
├─────────────────────────────────────────────────────────────────────────────┤
│ read:  velocities_solved[], bodies_curr (positions)                         │
│ write: bodies_next (positions, orientations), sleep_flags[]                 │
└─────────────────────────────────────────────────────────────────────────────┘
                                              │
                                              ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ STAGE 13: CACHE COMMIT + EVENTS                                       [SYNC] │
├─────────────────────────────────────────────────────────────────────────────┤
│ read:  manifolds[], solver lambda values                                    │
│ write: manifold_cache (persist warmstart data), impact_events[]             │
└─────────────────────────────────────────────────────────────────────────────┘
                                              │
                    ╔═════════════════════════════════════════════════════════╗
                    ║                 END SUBSTEP LOOP                        ║
                    ╚═════════════════════════════════════════════════════════╝
                                              │
                                              ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ STAGE 14: BUFFER SWAP                                                 [SYNC] │
├─────────────────────────────────────────────────────────────────────────────┤
│ swap(bodies_curr, bodies_next)                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## Phase 0: Foundation Data Structures

**Goal:** Define all core data structures needed by the pipeline.

No simulation yet—just the types, pools, and arenas.

### Step 0.1: Core Math Types

**Files:**
- `include/ferrum/physics/phys_types.h`

**Structures:**
```c
// Re-export or alias engine math types for physics use
typedef struct phys_vec3_t { float x, y, z; } phys_vec3_t;
typedef struct phys_quat_t { float x, y, z, w; } phys_quat_t;
typedef struct phys_mat3_t { float m[9]; } phys_mat3_t;  // 3x3 for inertia tensor
```

**Acceptance Criteria:**
- [ ] Types defined with predictable layout
- [ ] Conversion macros to/from engine math types

---

### Step 0.2: Rigid Body Structure

**Files:**
- `include/ferrum/physics/body.h`
- `src/physics/body/body.c`
- `tests/physics/body_tests.c`

**Structure:**
```c
typedef struct phys_body_t {
    phys_vec3_t position;         // 12 bytes
    phys_quat_t orientation;      // 16 bytes
    phys_vec3_t linear_vel;       // 12 bytes
    phys_vec3_t angular_vel;      // 12 bytes
    float inv_mass;               // 4 bytes
    phys_vec3_t inv_inertia_diag; // 12 bytes (diagonal approx)
    uint32_t flags;               // 4 bytes (sleeping, static, kinematic)
    uint8_t tier;                 // 1 byte
    uint8_t pad[3];               // alignment
} phys_body_t;                    // 76 bytes → pad to 80
```

**API:**
```c
void phys_body_init(phys_body_t *body);
void phys_body_set_mass(phys_body_t *body, float mass);
void phys_body_set_box_inertia(phys_body_t *body, float mass, phys_vec3_t half_extents);
void phys_body_set_sphere_inertia(phys_body_t *body, float mass, float radius);
void phys_body_set_capsule_inertia(phys_body_t *body, float mass, float radius, float half_height);
bool phys_body_is_static(const phys_body_t *body);
bool phys_body_is_sleeping(const phys_body_t *body);
```

**Test Cases:**
```c
// test_body_init_zeroed
phys_body_t b;
phys_body_init(&b);
ASSERT(b.inv_mass == 0.0f);  // static by default
ASSERT(phys_body_is_static(&b));

// test_set_mass_computes_inv_mass
phys_body_set_mass(&b, 2.0f);
ASSERT_FLOAT_NEAR(b.inv_mass, 0.5f, 0.001f);

// test_sphere_inertia
phys_body_set_sphere_inertia(&b, 1.0f, 0.5f);
// I = 2/5 * m * r^2 = 0.1, inv = 10
ASSERT_FLOAT_NEAR(b.inv_inertia_diag.x, 10.0f, 0.1f);
```

**Acceptance Criteria:**
- [ ] Body structure exactly 80 bytes
- [ ] Mass/inertia setters compute correct inverse values
- [ ] Static/sleeping/kinematic flags work correctly

---

### Step 0.3: Collider Structures (Sphere, Box, Capsule)

**Files:**
- `include/ferrum/physics/collider.h`
- `src/physics/collider/collider.c`
- `tests/physics/collider_tests.c`

**Structures:**
```c
typedef enum phys_shape_type_t {
    PHYS_SHAPE_SPHERE = 0,
    PHYS_SHAPE_BOX,
    PHYS_SHAPE_CAPSULE,
    PHYS_SHAPE_CONVEX,    // future
    PHYS_SHAPE_MESH,      // future
} phys_shape_type_t;

typedef struct phys_sphere_t {
    float radius;
} phys_sphere_t;

typedef struct phys_box_t {
    phys_vec3_t half_extents;
} phys_box_t;

typedef struct phys_capsule_t {
    float radius;
    float half_height;  // height of cylinder segment / 2
} phys_capsule_t;

typedef struct phys_collider_t {
    phys_shape_type_t type;
    uint32_t shape_index;      // into shape-specific pool
    phys_vec3_t local_offset;  // offset from body origin
    phys_quat_t local_rotation;
} phys_collider_t;
```

**API:**
```c
void phys_collider_init_sphere(phys_collider_t *c, uint32_t sphere_idx, phys_vec3_t offset);
void phys_collider_init_box(phys_collider_t *c, uint32_t box_idx, phys_vec3_t offset, phys_quat_t rotation);
void phys_collider_init_capsule(phys_collider_t *c, uint32_t capsule_idx, phys_vec3_t offset, phys_quat_t rotation);
```

**Acceptance Criteria:**
- [ ] All three primitive types defined
- [ ] Local transform (offset + rotation) supported
- [ ] Shape index indirection to shape-specific pools

---

### Step 0.4: AABB Structure

**Files:**
- `include/ferrum/physics/aabb.h`
- `src/physics/collision/aabb.c`
- `tests/physics/aabb_tests.c`

**Structure:**
```c
typedef struct phys_aabb_t {
    phys_vec3_t min;
    phys_vec3_t max;
} phys_aabb_t;  // 24 bytes
```

**API:**
```c
void phys_aabb_from_sphere(phys_aabb_t *aabb, phys_vec3_t center, float radius);
void phys_aabb_from_box(phys_aabb_t *aabb, phys_vec3_t center, phys_quat_t rotation, phys_vec3_t half_extents);
void phys_aabb_from_capsule(phys_aabb_t *aabb, phys_vec3_t center, phys_quat_t rotation, float radius, float half_height);
bool phys_aabb_overlap(const phys_aabb_t *a, const phys_aabb_t *b);
void phys_aabb_merge(phys_aabb_t *out, const phys_aabb_t *a, const phys_aabb_t *b);
void phys_aabb_expand(phys_aabb_t *aabb, float margin);
```

**Test Cases:**
```c
// test_aabb_sphere
phys_aabb_t aabb;
phys_aabb_from_sphere(&aabb, (phys_vec3_t){5, 10, 15}, 2.0f);
ASSERT_VEC3_EQ(aabb.min, (phys_vec3_t){3, 8, 13});
ASSERT_VEC3_EQ(aabb.max, (phys_vec3_t){7, 12, 17});

// test_aabb_box_rotated
// 45° rotation around Y should expand the X/Z extents
phys_quat_t rot45 = quat_from_axis_angle((phys_vec3_t){0,1,0}, M_PI/4);
phys_aabb_from_box(&aabb, (phys_vec3_t){0,0,0}, rot45, (phys_vec3_t){1, 0.5f, 0.5f});
// Check that X extent is larger than 1 due to rotation

// test_aabb_overlap
phys_aabb_t a = {{0,0,0}, {2,2,2}};
phys_aabb_t b = {{1,1,1}, {3,3,3}};
ASSERT(phys_aabb_overlap(&a, &b));

phys_aabb_t c = {{5,5,5}, {6,6,6}};
ASSERT(!phys_aabb_overlap(&a, &c));
```

**Acceptance Criteria:**
- [ ] Correct AABB computation for all three primitives
- [ ] Rotated box AABB is world-aligned bounding box
- [ ] Overlap test works for touching and separated AABBs

---

### Step 0.5: Body Pool and Frame Arena

**Files:**
- `include/ferrum/physics/phys_pool.h`
- `src/physics/memory/phys_pool.c`
- `src/physics/memory/phys_arena.c`
- `tests/physics/phys_pool_tests.c`

**API:**
```c
// Body pool (wraps engine pool_t with physics-specific interface)
typedef struct phys_body_pool_t {
    pool_t pool;
    phys_body_t *bodies_curr;
    phys_body_t *bodies_next;
} phys_body_pool_t;

int phys_body_pool_init(phys_body_pool_t *pool, uint32_t capacity);
void phys_body_pool_destroy(phys_body_pool_t *pool);
pool_handle_t phys_body_pool_alloc(phys_body_pool_t *pool);
void phys_body_pool_free(phys_body_pool_t *pool, pool_handle_t h);
phys_body_t *phys_body_pool_get_curr(phys_body_pool_t *pool, pool_handle_t h);
phys_body_t *phys_body_pool_get_next(phys_body_pool_t *pool, pool_handle_t h);
void phys_body_pool_swap_buffers(phys_body_pool_t *pool);
uint32_t phys_body_pool_active_count(const phys_body_pool_t *pool);

// Frame arena (wraps engine arena_t)
typedef struct phys_frame_arena_t {
    arena_t arena;
} phys_frame_arena_t;

int phys_frame_arena_init(phys_frame_arena_t *arena, size_t size);
void phys_frame_arena_destroy(phys_frame_arena_t *arena);
void *phys_frame_arena_alloc(phys_frame_arena_t *arena, size_t size, size_t align);
void phys_frame_arena_reset(phys_frame_arena_t *arena);
size_t phys_frame_arena_used(const phys_frame_arena_t *arena);
```

**Acceptance Criteria:**
- [ ] Pool provides double-buffered body access
- [ ] Buffer swap is O(1) pointer exchange
- [ ] Arena allocations aligned properly
- [ ] Arena reset is O(1)

---

### Step 0.6: Tier List Structure

**Files:**
- `include/ferrum/physics/tier_list.h`
- `src/physics/tier/tier_list.c`
- `tests/physics/tier_list_tests.c`

**Structure:**
```c
#define PHYS_TIER_COUNT 6  // T0-T5

typedef struct phys_tier_list_t {
    uint32_t *indices;   // arena-allocated
    uint32_t count;
    uint32_t capacity;
} phys_tier_list_t;

typedef struct phys_tier_lists_t {
    phys_tier_list_t tiers[PHYS_TIER_COUNT];
} phys_tier_lists_t;
```

**API:**
```c
void phys_tier_lists_init(phys_tier_lists_t *lists, phys_frame_arena_t *arena, uint32_t max_bodies);
void phys_tier_list_add(phys_tier_list_t *list, uint32_t body_index);
void phys_tier_list_clear(phys_tier_list_t *list);
```

**Acceptance Criteria:**
- [ ] 6 tier lists (T0-T5) available
- [ ] Lists are arena-allocated (no malloc during tick)
- [ ] Add is O(1), clear is O(1)

---

### Step 0.7: Spatial Hash Grid

**Files:**
- `include/ferrum/physics/spatial_grid.h`
- `src/physics/broadphase/spatial_grid.c`
- `tests/physics/spatial_grid_tests.c`

**Structure:**
```c
typedef struct phys_grid_cell_t {
    uint32_t *body_indices;
    uint32_t count;
    uint32_t capacity;
} phys_grid_cell_t;

typedef struct phys_spatial_grid_t {
    phys_grid_cell_t *cells;  // hash table
    uint32_t cell_count;      // power of 2
    float cell_size;
    phys_frame_arena_t *arena; // for cell allocations
} phys_spatial_grid_t;
```

**API:**
```c
void phys_spatial_grid_init(phys_spatial_grid_t *grid, uint32_t cell_count, float cell_size, phys_frame_arena_t *arena);
void phys_spatial_grid_clear(phys_spatial_grid_t *grid);
void phys_spatial_grid_insert(phys_spatial_grid_t *grid, uint32_t body_index, const phys_aabb_t *aabb);
void phys_spatial_grid_query(const phys_spatial_grid_t *grid, const phys_aabb_t *aabb, 
                             uint32_t *out_indices, uint32_t max_results, uint32_t *out_count);
```

**Acceptance Criteria:**
- [ ] Bodies inserted into correct cells based on AABB
- [ ] Large AABBs inserted into multiple cells
- [ ] Query returns all bodies overlapping query AABB
- [ ] No heap allocations during insert/query (uses arena)

---

### Step 0.8: Contact and Manifold Structures

**Files:**
- `include/ferrum/physics/manifold.h`
- `src/physics/collision/manifold.c`
- `tests/physics/manifold_tests.c`

**Structures:**
```c
typedef struct phys_contact_point_t {
    phys_vec3_t point_world;    // contact point in world space
    phys_vec3_t normal;         // points from A to B
    float penetration;          // positive = overlap
    uint32_t feature_id;        // for persistent contact tracking
} phys_contact_point_t;

#define PHYS_MAX_MANIFOLD_POINTS 4

typedef struct phys_manifold_t {
    uint32_t body_a;
    uint32_t body_b;
    uint8_t point_count;
    phys_contact_point_t points[PHYS_MAX_MANIFOLD_POINTS];
    float friction;             // combined material friction
    float restitution;          // combined material restitution
    // Warmstarting data
    float normal_impulse_sum[PHYS_MAX_MANIFOLD_POINTS];
    float tangent_impulse_sum[PHYS_MAX_MANIFOLD_POINTS][2];
} phys_manifold_t;
```

**API:**
```c
void phys_manifold_init(phys_manifold_t *m, uint32_t body_a, uint32_t body_b);
void phys_manifold_add_point(phys_manifold_t *m, const phys_contact_point_t *point);
void phys_manifold_reduce(phys_manifold_t *m);  // keep best K points
void phys_manifold_cache_impulses(phys_manifold_t *m, const float *normal_impulses, const float tangent_impulses[][2]);
```

**Acceptance Criteria:**
- [ ] Manifold holds up to 4 contact points
- [ ] Point reduction keeps best contact configuration
- [ ] Warmstart impulses stored per-point
- [ ] Feature IDs enable persistent tracking across frames

---

### Step 0.9: Manifold Cache (Persistent)

**Files:**
- `include/ferrum/physics/manifold_cache.h`
- `src/physics/collision/manifold_cache.c`
- `tests/physics/manifold_cache_tests.c`

**Structure:**
```c
typedef struct phys_manifold_cache_entry_t {
    uint64_t pair_key;  // body_a << 32 | body_b (sorted)
    phys_manifold_t manifold;
    uint32_t age;       // frames since last contact (for expiry)
} phys_manifold_cache_entry_t;

typedef struct phys_manifold_cache_t {
    phys_manifold_cache_entry_t *entries;
    uint32_t capacity;
    uint32_t count;
} phys_manifold_cache_t;
```

**API:**
```c
void phys_manifold_cache_init(phys_manifold_cache_t *cache, uint32_t capacity);
void phys_manifold_cache_destroy(phys_manifold_cache_t *cache);
phys_manifold_t *phys_manifold_cache_find(phys_manifold_cache_t *cache, uint32_t body_a, uint32_t body_b);
phys_manifold_t *phys_manifold_cache_insert(phys_manifold_cache_t *cache, uint32_t body_a, uint32_t body_b);
void phys_manifold_cache_expire_old(phys_manifold_cache_t *cache, uint32_t max_age);
```

**Acceptance Criteria:**
- [ ] O(1) average lookup by body pair
- [ ] Warmstart data persists across frames
- [ ] Old entries expire after K frames without contact

---

### Step 0.10: Constraint and Jacobian Structures

**Files:**
- `include/ferrum/physics/constraint.h`
- `src/physics/solver/constraint.c`
- `tests/physics/constraint_tests.c`

**Structures:**
```c
typedef struct phys_jacobian_row_t {
    phys_vec3_t J_va;    // linear Jacobian for body A
    phys_vec3_t J_wa;    // angular Jacobian for body A
    phys_vec3_t J_vb;    // linear Jacobian for body B
    phys_vec3_t J_wb;    // angular Jacobian for body B
    float effective_mass;
    float bias;
    float lambda;        // accumulated impulse
    float lambda_min;
    float lambda_max;
} phys_jacobian_row_t;

typedef struct phys_constraint_t {
    uint32_t body_a;
    uint32_t body_b;
    uint8_t row_count;   // 1 for normal, 3 for normal + 2 friction
    phys_jacobian_row_t rows[3];
} phys_constraint_t;
```

**API:**
```c
void phys_constraint_build_contact(phys_constraint_t *c, 
    const phys_body_t *body_a, const phys_body_t *body_b,
    const phys_contact_point_t *contact,
    float friction, float restitution, float dt);
```

**Acceptance Criteria:**
- [ ] Contact constraint generates 1 normal + 2 friction rows
- [ ] Effective mass computed correctly for point constraints
- [ ] Bias includes Baumgarte stabilization and restitution

---

### Step 0.11: Island Structure

**Files:**
- `include/ferrum/physics/island.h`
- `src/physics/solver/island.c`
- `tests/physics/island_tests.c`

**Structures:**
```c
typedef struct phys_island_t {
    uint32_t *body_indices;
    uint32_t body_count;
    uint32_t *constraint_indices;
    uint32_t constraint_count;
} phys_island_t;

typedef struct phys_island_list_t {
    phys_island_t *islands;
    uint32_t count;
    uint32_t capacity;
    // Union-find workspace
    uint32_t *parent;
    uint32_t *rank;
} phys_island_list_t;
```

**API:**
```c
void phys_island_list_init(phys_island_list_t *list, phys_frame_arena_t *arena, uint32_t max_bodies, uint32_t max_islands);
void phys_island_list_build(phys_island_list_t *list, const phys_constraint_t *constraints, uint32_t constraint_count, uint32_t body_count);
```

**Acceptance Criteria:**
- [ ] Union-find correctly groups connected bodies
- [ ] Each island contains body and constraint indices
- [ ] Independent islands have no overlap

---

### Step 0.12: Physics World Container

**Files:**
- `include/ferrum/physics/world.h`
- `src/physics/world/world.c`
- `tests/physics/world_tests.c`

**Structure:**
```c
typedef struct phys_world_config_t {
    uint32_t max_bodies;
    uint32_t max_colliders;
    uint32_t manifold_cache_size;
    size_t frame_arena_size;
    float fixed_dt;
    phys_vec3_t gravity;
    uint32_t substeps;
    uint32_t solver_iterations;
} phys_world_config_t;

typedef struct phys_world_t {
    phys_world_config_t config;
    
    // Pools
    phys_body_pool_t body_pool;
    pool_t collider_pool;
    pool_t sphere_pool;
    pool_t box_pool;
    pool_t capsule_pool;
    
    // Persistent
    phys_aabb_t *aabbs;
    phys_manifold_cache_t manifold_cache;
    
    // Per-tick arena
    phys_frame_arena_t frame_arena;
    
    // Per-tick transients (allocated from frame_arena)
    phys_tier_lists_t tier_lists;
    phys_spatial_grid_t spatial_grid;
    
    uint64_t tick_count;
} phys_world_t;
```

**API:**
```c
int phys_world_init(phys_world_t *world, const phys_world_config_t *config);
void phys_world_destroy(phys_world_t *world);
pool_handle_t phys_world_create_body(phys_world_t *world);
void phys_world_destroy_body(phys_world_t *world, pool_handle_t h);
phys_body_t *phys_world_get_body(phys_world_t *world, pool_handle_t h);
void phys_world_set_sphere_collider(phys_world_t *world, pool_handle_t body, float radius);
void phys_world_set_box_collider(phys_world_t *world, pool_handle_t body, phys_vec3_t half_extents);
void phys_world_set_capsule_collider(phys_world_t *world, pool_handle_t body, float radius, float half_height);
```

**Acceptance Criteria:**
- [ ] World initializes all subsystems
- [ ] Body creation allocates from pool
- [ ] Collider setters allocate shape and link to body
- [ ] Destroy frees all memory cleanly

---

### Step 0.13: Phase 0 Integration Test

**Files:**
- `tests/physics/phase0_integration_tests.c`

**Test Cases:**
```c
// test_create_world_and_bodies
phys_world_config_t cfg = { .max_bodies = 1000, ... };
phys_world_t world;
phys_world_init(&world, &cfg);

pool_handle_t bodies[100];
for (int i = 0; i < 100; ++i) {
    bodies[i] = phys_world_create_body(&world);
    phys_body_t *b = phys_world_get_body(&world, bodies[i]);
    phys_body_set_mass(b, 1.0f);
    phys_world_set_sphere_collider(&world, bodies[i], 0.5f);
}

ASSERT(phys_body_pool_active_count(&world.body_pool) == 100);

phys_world_destroy(&world);
// Verify no leaks with valgrind/ASan
```

**Acceptance Criteria:**
- [ ] All data structures work together
- [ ] No memory leaks
- [ ] Clean initialization and destruction

---

## Phase 1: Complete Pipeline (Single-Threaded)

**Goal:** Implement all 14 stages with sphere-only colliders, single-threaded.
Full pipeline structure from the start—no simplified versions.

This phase delivers:
- Complete tick structure with substeps
- Tier classification (initially all T0)
- Spatial grid broadphase
- Sphere-sphere narrowphase
- Manifold caching with warmstart
- Stabilization hints
- Contact constraints (normal + friction)
- Island detection
- TGS solver
- Integration with sleep
- Network snapshot encoding/decoding

---

### Step 1.1: Step Plan Stage (Stage 0)

**Files:**
- `include/ferrum/physics/step_plan.h`
- `src/physics/stages/step_plan.c`
- `tests/physics/step_plan_tests.c`

**Structure:**
```c
typedef struct phys_step_plan_t {
    uint32_t substeps;
    uint32_t solver_iterations;
    float dt;
    // Per-tier parameters (for future tiered simulation)
    struct {
        bool active;
        uint32_t iterations;
    } tier_params[PHYS_TIER_COUNT];
} phys_step_plan_t;
```

**API:**
```c
void phys_stage_step_plan(phys_step_plan_t *plan, const phys_world_t *world, const phys_game_state_t *game);
```

**Acceptance Criteria:**
- [ ] Plan computed from world config
- [ ] Substeps and iterations configurable
- [ ] Future: per-tier budgets

---

### Step 1.2: Tier Classification Stage (Stage 1)

**Files:**
- `include/ferrum/physics/tier_classify.h`
- `src/physics/stages/tier_classify.c`
- `tests/physics/tier_classify_tests.c`

**API:**
```c
typedef struct phys_tier_classify_args_t {
    const phys_body_t *bodies;
    uint32_t body_count;
    const phys_vec3_t *player_positions;
    uint32_t player_count;
    phys_tier_lists_t *tier_lists_out;
    phys_frame_arena_t *arena;
} phys_tier_classify_args_t;

void phys_stage_tier_classify(const phys_tier_classify_args_t *args);
```

For Phase 1, all active bodies go to T0 (simplest case).
Later phases add distance-based classification with hysteresis.

**Acceptance Criteria:**
- [ ] All non-sleeping bodies classified into tiers
- [ ] Tier lists populated correctly
- [ ] Arena-allocated index arrays

---

### Step 1.3: Spatial Index Update Stage (Stage 2)

**Files:**
- `include/ferrum/physics/spatial_update.h`
- `src/physics/stages/spatial_update.c`
- `tests/physics/spatial_update_tests.c`

**API:**
```c
typedef struct phys_spatial_update_args_t {
    const phys_body_t *bodies;
    const phys_collider_t *colliders;
    const void *shape_pools[3];  // sphere, box, capsule
    phys_aabb_t *aabbs_out;
    phys_spatial_grid_t *grid_out;
    uint32_t body_count;
} phys_spatial_update_args_t;

void phys_stage_spatial_update(const phys_spatial_update_args_t *args);
```

**Acceptance Criteria:**
- [ ] AABBs computed for all bodies
- [ ] Grid populated with body indices
- [ ] Correct AABB for sphere colliders

---

### Step 1.4: Halo Closure Stage (Stage 3)

**Files:**
- `include/ferrum/physics/halo_closure.h`
- `src/physics/stages/halo_closure.c`
- `tests/physics/halo_closure_tests.c`

**API:**
```c
typedef struct phys_halo_closure_args_t {
    const phys_body_t *bodies;
    const phys_aabb_t *aabbs;
    const phys_spatial_grid_t *grid;
    phys_tier_lists_t *tier_lists;  // modified in place
    float velocity_margin;
    float dt;
} phys_halo_closure_args_t;

void phys_stage_halo_closure(const phys_halo_closure_args_t *args);
```

For T0 bodies: expand AABB by velocity*dt + margin, query grid, promote neighbors to T1.

**Acceptance Criteria:**
- [ ] Fast-moving T0 bodies promote nearby bodies
- [ ] Promotion is conservative (includes potential contacts)
- [ ] Hysteresis prevents tier flapping

---

### Step 1.5: AABB Update Stage (Stage 4)

**Files:**
- `include/ferrum/physics/aabb_update.h`
- `src/physics/stages/aabb_update.c`
- `tests/physics/aabb_update_tests.c`

**API:**
```c
typedef struct phys_aabb_update_args_t {
    const phys_body_t *bodies;
    const phys_collider_t *colliders;
    const void *shape_pools[3];
    phys_aabb_t *aabbs_out;
    const phys_tier_lists_t *tier_lists;  // only update active tiers
} phys_aabb_update_args_t;

void phys_stage_aabb_update(const phys_aabb_update_args_t *args);
```

**Acceptance Criteria:**
- [ ] Only active bodies (T0-T4) have AABBs updated
- [ ] Sleeping bodies (T5) skipped

---

### Step 1.6: Broadphase Stage (Stage 5)

**Files:**
- `include/ferrum/physics/broadphase.h`
- `src/physics/stages/broadphase.c`
- `tests/physics/broadphase_tests.c`

**Structure:**
```c
typedef struct phys_collision_pair_t {
    uint32_t body_a;
    uint32_t body_b;
} phys_collision_pair_t;
```

**API:**
```c
typedef struct phys_broadphase_args_t {
    const phys_aabb_t *aabbs;
    const phys_spatial_grid_t *grid;
    phys_collision_pair_t *pairs_out;
    uint32_t max_pairs;
    uint32_t *pair_count_out;
    phys_frame_arena_t *arena;
} phys_broadphase_args_t;

void phys_stage_broadphase(const phys_broadphase_args_t *args);
```

Grid-based broadphase: for each cell, test all pairs within cell.
Deduplicate pairs (sorted indices, hash set).

**Acceptance Criteria:**
- [ ] All overlapping AABB pairs detected
- [ ] No duplicate pairs
- [ ] No self-pairs (a,a)
- [ ] Pairs sorted (body_a < body_b)

---

### Step 1.7: Narrowphase Stage (Stage 6)

**Files:**
- `include/ferrum/physics/narrowphase.h`
- `src/physics/collision/narrowphase_sphere.c`
- `src/physics/collision/narrowphase_box.c`
- `src/physics/collision/narrowphase_capsule.c`
- `src/physics/stages/narrowphase.c`
- `tests/physics/narrowphase_tests.c`

**API:**
```c
typedef struct phys_contact_candidate_t {
    uint32_t body_a;
    uint32_t body_b;
    phys_contact_point_t contacts[PHYS_MAX_MANIFOLD_POINTS];
    uint8_t contact_count;
} phys_contact_candidate_t;

typedef struct phys_narrowphase_args_t {
    const phys_body_t *bodies;
    const phys_collider_t *colliders;
    const void *shape_pools[3];
    const phys_collision_pair_t *pairs;
    uint32_t pair_count;
    phys_contact_candidate_t *candidates_out;
    uint32_t *candidate_count_out;
    uint32_t max_candidates;
} phys_narrowphase_args_t;

void phys_stage_narrowphase(const phys_narrowphase_args_t *args);

// Primitive tests (internal)
bool phys_sphere_vs_sphere(const phys_sphere_t *a, phys_vec3_t pos_a,
                           const phys_sphere_t *b, phys_vec3_t pos_b,
                           phys_contact_point_t *contact_out);
bool phys_box_vs_box(const phys_box_t *a, phys_vec3_t pos_a, phys_quat_t rot_a,
                     const phys_box_t *b, phys_vec3_t pos_b, phys_quat_t rot_b,
                     phys_contact_point_t contacts_out[], uint8_t *count_out);
bool phys_capsule_vs_capsule(const phys_capsule_t *a, phys_vec3_t pos_a, phys_quat_t rot_a,
                             const phys_capsule_t *b, phys_vec3_t pos_b, phys_quat_t rot_b,
                             phys_contact_point_t *contact_out);
// Cross-type tests
bool phys_sphere_vs_box(...);
bool phys_sphere_vs_capsule(...);
bool phys_box_vs_capsule(...);
```

**Phase 1:** Sphere-sphere only. Box and capsule added in Phase 2.

**Acceptance Criteria:**
- [ ] Correct contact point, normal, penetration
- [ ] Normal points from A to B
- [ ] Feature IDs computed for persistent tracking
- [ ] No contact generated for separated shapes

---

### Step 1.8: Manifold Build + Cache Merge Stage (Stage 7)

**Files:**
- `include/ferrum/physics/manifold_build.h`
- `src/physics/stages/manifold_build.c`
- `tests/physics/manifold_build_tests.c`

**API:**
```c
typedef struct phys_manifold_build_args_t {
    const phys_contact_candidate_t *candidates;
    uint32_t candidate_count;
    phys_manifold_cache_t *cache;  // read/write
    phys_manifold_t *manifolds_out;  // arena-allocated
    uint32_t *manifold_count_out;
    uint32_t max_manifolds;
    phys_frame_arena_t *arena;
} phys_manifold_build_args_t;

void phys_stage_manifold_build(const phys_manifold_build_args_t *args);
```

Merge new contacts with cached contacts:
1. Find cached manifold for pair
2. Match new contacts to cached by feature ID
3. Keep warmstart impulses for matched contacts
4. Reduce to ≤4 best points
5. Output manifold for this frame

**Acceptance Criteria:**
- [ ] Warmstart impulses preserved for matching contacts
- [ ] Point count reduced to ≤4
- [ ] New pairs create new cache entries
- [ ] Old cache entries expire

---

### Step 1.9: Stabilization Hints Stage (Stage 8)

**Files:**
- `include/ferrum/physics/stabilization.h`
- `src/physics/stages/stabilization.c`
- `tests/physics/stabilization_tests.c`

**Structure:**
```c
typedef struct phys_stab_hint_t {
    float friction_scale;     // multiplier on base friction (e.g., 3.0 for resting)
    float restitution_scale;  // multiplier (0.0 to suppress bounce when resting)
} phys_stab_hint_t;
```

**API:**
```c
typedef struct phys_stabilization_args_t {
    const phys_manifold_t *manifolds;
    uint32_t manifold_count;
    const phys_body_t *bodies;
    const phys_tier_lists_t *tier_lists;
    phys_stab_hint_t *hints_out;
} phys_stabilization_args_t;

void phys_stage_stabilization(const phys_stabilization_args_t *args);
```

Classify contacts:
- **Resting:** low relative velocity → boost friction, suppress restitution
- **Sliding:** higher velocity → normal friction
- **Separating:** negative relative velocity → normal response

**Acceptance Criteria:**
- [ ] Resting contacts get friction boost (~3x)
- [ ] Resting contacts get restitution suppression (→0)
- [ ] Fast contacts keep normal parameters
- [ ] Tier affects thresholds (T0 more aggressive stabilization)

---

### Step 1.10: Constraint Build Stage (Stage 9)

**Files:**
- `include/ferrum/physics/constraint_build.h`
- `src/physics/stages/constraint_build.c`
- `tests/physics/constraint_build_tests.c`

**API:**
```c
typedef struct phys_constraint_build_args_t {
    const phys_manifold_t *manifolds;
    const phys_stab_hint_t *hints;
    uint32_t manifold_count;
    const phys_body_t *bodies;
    phys_constraint_t *constraints_out;
    uint32_t *constraint_count_out;
    uint32_t max_constraints;
    float dt;
    float baumgarte;
} phys_constraint_build_args_t;

void phys_stage_constraint_build(const phys_constraint_build_args_t *args);
```

For each manifold contact:
- Build 1 normal constraint row
- Build 2 friction constraint rows (tangent directions)
- Apply stabilization hints (scaled friction/restitution)
- Warmstart: initialize lambda from cache

**Acceptance Criteria:**
- [ ] 3 rows per contact (normal + 2 friction)
- [ ] Jacobians computed correctly
- [ ] Effective mass precomputed
- [ ] Warmstart impulses loaded

---

### Step 1.11: Island Build Stage (Stage 10)

**Files:**
- `include/ferrum/physics/island_build.h`
- `src/physics/stages/island_build.c`
- `tests/physics/island_build_tests.c`

**API:**
```c
typedef struct phys_island_build_args_t {
    const phys_constraint_t *constraints;
    uint32_t constraint_count;
    uint32_t body_count;
    phys_island_list_t *islands_out;
    phys_frame_arena_t *arena;
} phys_island_build_args_t;

void phys_stage_island_build(const phys_island_build_args_t *args);
```

Union-find algorithm:
1. Initialize each body as its own set
2. For each constraint, union body_a and body_b
3. Group constraints and bodies by root
4. Output island structures

**Acceptance Criteria:**
- [ ] Connected components correctly identified
- [ ] Each island has list of bodies and constraints
- [ ] Isolated bodies form single-body islands (or are skipped)
- [ ] O(n α(n)) complexity

---

### Step 1.12: TGS Solve Stage (Stage 11)

**Files:**
- `include/ferrum/physics/tgs_solve.h`
- `src/physics/solver/tgs_solve.c`
- `tests/physics/tgs_solve_tests.c`

**API:**
```c
typedef struct phys_velocity_t {
    phys_vec3_t linear;
    phys_vec3_t angular;
} phys_velocity_t;

typedef struct phys_tgs_solve_args_t {
    const phys_island_list_t *islands;
    phys_constraint_t *constraints;  // modified (lambda updated)
    const phys_body_t *bodies_in;
    phys_velocity_t *velocities_out;  // delta velocities
    uint32_t body_count;
    uint32_t iterations;
} phys_tgs_solve_args_t;

void phys_stage_tgs_solve(const phys_tgs_solve_args_t *args);
```

TGS (Temporal Gauss-Seidel) algorithm:
```
for each island:
    copy velocities from bodies_in
    for iter in 0..iterations:
        for each constraint row:
            Δλ = (bias - J·v) / effective_mass
            λ_new = clamp(λ + Δλ, λ_min, λ_max)
            Δλ = λ_new - λ
            λ = λ_new
            v_a += inv_M_a * J_a^T * Δλ
            v_b += inv_M_b * J_b^T * Δλ
    write final velocities to velocities_out
```

**Acceptance Criteria:**
- [ ] Converges for stable stacking
- [ ] Friction prevents sliding on resting contacts
- [ ] Restitution produces correct bounce velocity
- [ ] Islands solved independently

---

### Step 1.13: Integrate + Sleep Stage (Stage 12)

**Files:**
- `include/ferrum/physics/integrate.h`
- `src/physics/stages/integrate.c`
- `tests/physics/integrate_tests.c`

**API:**
```c
typedef struct phys_integrate_args_t {
    const phys_body_t *bodies_in;
    const phys_velocity_t *velocities;
    phys_body_t *bodies_out;
    uint32_t body_count;
    float dt;
    phys_vec3_t gravity;
    float sleep_threshold_linear;
    float sleep_threshold_angular;
    uint32_t sleep_delay_frames;
} phys_integrate_args_t;

void phys_stage_integrate(const phys_integrate_args_t *args);
```

Semi-implicit Euler:
```
v += a * dt  // gravity applied before (or part of solve)
p += v * dt
q += 0.5 * ω * q * dt  // quaternion integration
normalize(q)

// Sleep check
if |v| < threshold_linear && |ω| < threshold_angular for N frames:
    mark sleeping
```

**Acceptance Criteria:**
- [ ] Positions and orientations updated correctly
- [ ] Quaternion normalized after integration
- [ ] Sleep detection works with hysteresis
- [ ] Static bodies unchanged

---

### Step 1.14: Cache Commit + Events Stage (Stage 13)

**Files:**
- `include/ferrum/physics/cache_commit.h`
- `src/physics/stages/cache_commit.c`
- `tests/physics/cache_commit_tests.c`

**API:**
```c
typedef struct phys_impact_event_t {
    uint32_t body_a;
    uint32_t body_b;
    phys_vec3_t point;
    phys_vec3_t normal;
    float impulse_magnitude;
} phys_impact_event_t;

typedef struct phys_cache_commit_args_t {
    const phys_manifold_t *manifolds;
    const phys_constraint_t *constraints;
    uint32_t manifold_count;
    phys_manifold_cache_t *cache;
    phys_impact_event_t *events_out;
    uint32_t *event_count_out;
    uint32_t max_events;
    float impact_threshold;
} phys_cache_commit_args_t;

void phys_stage_cache_commit(const phys_cache_commit_args_t *args);
```

1. Copy solved lambda values back to manifold cache
2. Emit impact events for high-impulse contacts

**Acceptance Criteria:**
- [ ] Warmstart data persisted for next frame
- [ ] Impact events generated for significant collisions
- [ ] Cache ages updated

---

### Step 1.15: Buffer Swap Stage (Stage 14)

**Files:**
- `src/physics/world/tick.c` (part of tick function)

```c
void phys_world_tick(phys_world_t *world) {
    phys_frame_arena_reset(&world->frame_arena);
    
    phys_step_plan_t plan;
    phys_stage_step_plan(&plan, world, NULL);
    
    phys_stage_tier_classify(...);
    phys_stage_spatial_update(...);
    
    for (uint32_t substep = 0; substep < plan.substeps; ++substep) {
        phys_stage_halo_closure(...);
        phys_stage_aabb_update(...);
        phys_stage_broadphase(...);
        phys_stage_narrowphase(...);
        phys_stage_manifold_build(...);
        phys_stage_stabilization(...);
        phys_stage_constraint_build(...);
        phys_stage_island_build(...);
        phys_stage_tgs_solve(...);
        phys_stage_integrate(...);
        phys_stage_cache_commit(...);
    }
    
    phys_body_pool_swap_buffers(&world->body_pool);
    world->tick_count++;
}
```

**Acceptance Criteria:**
- [ ] Complete tick executes all stages
- [ ] Buffer swap is O(1)
- [ ] Tick count incremented

---

### Step 1.16: Network Snapshot Encoding

**Files:**
- `include/ferrum/physics/snapshot.h`
- `src/physics/net/snapshot_encode.c`
- `src/physics/net/snapshot_decode.c`
- `tests/physics/snapshot_tests.c`

**API:**
```c
typedef struct phys_snapshot_t {
    uint64_t tick;
    uint32_t body_count;
    // Quantized state per body
    struct {
        int16_t position[3];   // quantized to mm
        int16_t orientation[3]; // smallest-3 quaternion
        int16_t linear_vel[3];
        int16_t angular_vel[3];
        uint8_t flags;
    } *bodies;
} phys_snapshot_t;

size_t phys_snapshot_encode(const phys_world_t *world, uint8_t *buffer, size_t max_size);
int phys_snapshot_decode(phys_world_t *world, const uint8_t *buffer, size_t size);
size_t phys_snapshot_encode_delta(const phys_snapshot_t *prev, const phys_snapshot_t *curr, 
                                   uint8_t *buffer, size_t max_size);
int phys_snapshot_decode_delta(const phys_snapshot_t *prev, phys_snapshot_t *curr,
                                const uint8_t *buffer, size_t size);
```

**Acceptance Criteria:**
- [ ] Full snapshot encoding/decoding works
- [ ] Delta compression only sends changed bodies
- [ ] Quantization matches existing engine formats (vec3_mm, quat_snorm16)
- [ ] Round-trip error within acceptable bounds

---

### Step 1.17: Phase 1 Integration Test + Benchmark

**Files:**
- `tests/physics/phase1_integration_tests.c`
- `tests/physics/phase1_bench.c`

**Test Cases:**
```c
// test_sphere_stack_stable
// Create 5 spheres stacked vertically
// Run 300 ticks (10 seconds)
// Verify stack hasn't collapsed (top sphere still near expected height)

// test_sphere_falls_on_floor
// Create static floor + dynamic sphere above
// Run until sphere rests on floor
// Verify position correct, velocity near zero

// test_two_spheres_collide_elastic
// Two spheres approaching each other
// Verify momentum conservation after collision

// test_warmstart_improves_convergence
// Stack of spheres, measure solver iterations to convergence
// With warmstart: fewer iterations than without

// test_sleep_activates_correctly
// Sleeping sphere, dropped object lands on it
// Verify sleeping sphere wakes and responds

// test_network_snapshot_round_trip
// Encode world state, decode into new world
// Verify states match within quantization error
```

**Benchmarks:**
```c
// bench_100_spheres_30hz_2substeps
// Target: < 1.5 ms per tick

// bench_1000_spheres_30hz_2substeps
// Target: < 10 ms per tick

// bench_snapshot_encode_100_bodies
// Target: < 50 µs

// bench_snapshot_delta_100_bodies_10pct_changed
// Target: < 20 µs
```

**Acceptance Criteria:**
- [ ] All functional tests pass
- [ ] Stacking is stable (no drift or collapse)
- [ ] Performance within targets
- [ ] Network round-trip accurate

---

## Phase 2: Box and Capsule Colliders

**Goal:** Complete narrowphase for all primitive pairs.

9 collision pairs total:
- Sphere-Sphere ✓ (Phase 1)
- Sphere-Box
- Sphere-Capsule
- Box-Box
- Box-Capsule
- Capsule-Capsule

### Step 2.1: Sphere-Box Narrowphase

**Files:**
- `src/physics/collision/narrowphase_sphere_box.c`
- `tests/physics/narrowphase_sphere_box_tests.c`

**Algorithm:**
1. Transform sphere center to box local space
2. Find closest point on box surface
3. Compute distance and penetration
4. Generate contact in world space

**Test Cases:**
```c
// test_sphere_inside_box_face
// test_sphere_touching_box_edge
// test_sphere_touching_box_corner
// test_sphere_separated_from_box
```

---

### Step 2.2: Sphere-Capsule Narrowphase

**Files:**
- `src/physics/collision/narrowphase_sphere_capsule.c`
- `tests/physics/narrowphase_sphere_capsule_tests.c`

**Algorithm:**
1. Find closest point on capsule axis to sphere center
2. Sphere-sphere test from closest point

---

### Step 2.3: Box-Box Narrowphase

**Files:**
- `src/physics/collision/narrowphase_box_box.c`
- `tests/physics/narrowphase_box_box_tests.c`

**Algorithm:** SAT (Separating Axis Theorem) with 15 axes:
- 3 face normals of A
- 3 face normals of B
- 9 edge-edge cross products

Generate up to 4 contact points on reference face.

**Test Cases:**
```c
// test_boxes_face_contact
// test_boxes_edge_contact
// test_boxes_vertex_contact
// test_boxes_separated
// test_boxes_rotated_45
```

---

### Step 2.4: Box-Capsule Narrowphase

**Files:**
- `src/physics/collision/narrowphase_box_capsule.c`
- `tests/physics/narrowphase_box_capsule_tests.c`

**Algorithm:**
1. Find closest points between capsule axis and box edges
2. Sphere-box test from capsule end

---

### Step 2.5: Capsule-Capsule Narrowphase

**Files:**
- `src/physics/collision/narrowphase_capsule_capsule.c`
- `tests/physics/narrowphase_capsule_capsule_tests.c`

**Algorithm:**
1. Closest points between two line segments (capsule axes)
2. Sphere-sphere test from closest points

---

### Step 2.6: AABB for Box and Capsule

Extend `phys_stage_aabb_update` to compute correct AABBs for rotated boxes and capsules.

---

### Step 2.7: Phase 2 Integration Test + Benchmark

**Test Cases:**
```c
// test_box_stack_stable
// test_capsule_rolls_on_floor
// test_mixed_primitive_pile
// test_all_primitive_pairs_collide
```

**Benchmarks:**
```c
// bench_100_boxes_30hz
// bench_100_capsules_30hz
// bench_100_mixed_primitives
```

**Network Tests:**
```c
// test_snapshot_with_boxes_and_capsules
// test_delta_compression_mixed_types
```

---

## Phase 3: Parallel Jobs

**Goal:** Convert all parallel stages to use job system.

### Step 3.1: Job Infrastructure

**Files:**
- `include/ferrum/physics/phys_jobs.h`
- `src/physics/jobs/phys_job_dispatch.c`

**API:**
```c
typedef struct phys_job_context_t {
    job_system_t *job_sys;
    job_counter_t counters[PHYS_STAGE_COUNT];
} phys_job_context_t;

void phys_job_context_init(phys_job_context_t *ctx, job_system_t *sys);
void phys_dispatch_stage(phys_job_context_t *ctx, phys_stage_id_t stage, void *args, uint32_t count);
void phys_wait_stage(phys_job_context_t *ctx, phys_stage_id_t stage);
```

---

### Step 3.2: Parallelize Tier Classification

Split body range across jobs (1k bodies/job).

---

### Step 3.3: Parallelize Spatial Update

Split body range across jobs (512 bodies/job).

---

### Step 3.4: Parallelize Broadphase

Partition grid cells across jobs.

---

### Step 3.5: Parallelize Narrowphase

Split pair list across jobs (64 pairs/job).

---

### Step 3.6: Parallelize Manifold Build

Split candidate list across jobs (32 pairs/job).

---

### Step 3.7: Parallelize Stabilization

Split manifold list across jobs (64 manifolds/job).

---

### Step 3.8: Parallelize Constraint Build

Split manifold list across jobs.

---

### Step 3.9: Parallelize TGS Solve

One job per island (or batched small islands).
Islands are independent—zero write contention.

---

### Step 3.10: Parallelize Integrate

Split body range across jobs (512 bodies/job).

---

### Step 3.11: Phase 3 Integration Test + Benchmark

**Test Cases:**
```c
// test_parallel_determinism
// Run same scenario single-threaded and multi-threaded
// Results must be identical

// test_parallel_no_data_races
// ASan/TSan clean under high contention
```

**Benchmarks:**
```c
// bench_1000_bodies_4_threads
// bench_5000_bodies_8_threads
// bench_scaling_1_to_8_threads

// Network benchmark with parallel physics
// bench_snapshot_encode_parallel
```

---

## Phase 4: Tiered Simulation

**Goal:** Full tier system with per-tier parameters.

### Step 4.1: Distance-Based Tier Classification

Bodies classified by distance to nearest player:
- T0: < 5m (direct manipulation)
- T1: < 15m (near interactive)
- T2: < 50m (visible)
- T3: < 200m (world-shaping)
- T4: > 200m (background)
- T5: sleeping

Hysteresis to prevent flapping.

---

### Step 4.2: Per-Tier Solver Parameters

Different substeps/iterations per tier:
- T0: 3 substeps, 24 iterations
- T1: 2 substeps, 20 iterations
- T2: 1 substep, 16 iterations
- T3: 1 substep, 12 iterations
- T4: amortized (10 Hz), 8 iterations

---

### Step 4.3: Per-Tier Stabilization

T0 gets strongest stabilization (3x friction boost).
T4 gets minimal stabilization.

---

### Step 4.4: Amortized Ticking for T4

T4 bodies tick every 3rd frame (10 Hz instead of 30 Hz).
Interpolate visually.

---

### Step 4.5: Phase 4 Integration Test + Benchmark

**Test Cases:**
```c
// test_tier_promotion_on_approach
// test_tier_demotion_on_distance
// test_hysteresis_prevents_flapping
// test_t0_stability_vs_t4
```

**Benchmarks:**
```c
// bench_5000_bodies_tiered
// Compare tiered vs all-T0
```

**Network Tests:**
```c
// test_snapshot_with_tiered_bodies
// Verify tier state replicated correctly
```

---

## Phase 5: Raycasts and World Queries

**Goal:** Non-pipeline queries for gameplay.

### Step 5.1: Raycast Against Shapes

**Files:**
- `include/ferrum/physics/raycast.h`
- `src/physics/query/raycast.c`
- `tests/physics/raycast_tests.c`

**API:**
```c
typedef struct phys_ray_t {
    phys_vec3_t origin;
    phys_vec3_t direction;
    float max_distance;
} phys_ray_t;

typedef struct phys_raycast_hit_t {
    pool_handle_t body;
    phys_vec3_t point;
    phys_vec3_t normal;
    float distance;
} phys_raycast_hit_t;

bool phys_world_raycast(const phys_world_t *world, const phys_ray_t *ray, 
                         phys_raycast_hit_t *hit_out, uint32_t layer_mask);
uint32_t phys_world_raycast_all(const phys_world_t *world, const phys_ray_t *ray,
                                 phys_raycast_hit_t *hits_out, uint32_t max_hits, uint32_t layer_mask);
```

Uses spatial grid for broadphase, then precise shape intersection.

---

### Step 5.2: Shape Overlap Query

**API:**
```c
uint32_t phys_world_overlap_sphere(const phys_world_t *world, phys_vec3_t center, float radius,
                                    pool_handle_t *bodies_out, uint32_t max_bodies, uint32_t layer_mask);
uint32_t phys_world_overlap_box(const phys_world_t *world, phys_vec3_t center, phys_quat_t rotation,
                                 phys_vec3_t half_extents, pool_handle_t *bodies_out, uint32_t max_bodies, uint32_t layer_mask);
```

---

### Step 5.3: Closest Point Query

**API:**
```c
bool phys_world_closest_point(const phys_world_t *world, phys_vec3_t point,
                               float max_distance, phys_raycast_hit_t *hit_out, uint32_t layer_mask);
```

---

### Step 5.4: Phase 5 Integration Test + Benchmark

**Test Cases:**
```c
// test_raycast_hits_sphere
// test_raycast_hits_box
// test_raycast_misses
// test_overlap_sphere_finds_bodies
// test_layer_mask_filtering
```

**Benchmarks:**
```c
// bench_1000_raycasts_1000_bodies
// bench_overlap_query_dense_scene
```

---

## Phase 6: Static BVH

**Goal:** Efficient broadphase for static geometry.

### Step 6.1: BVH Build

Build BVH from static bodies using SAH (Surface Area Heuristic).

---

### Step 6.2: BVH Query

Query BVH for dynamic-vs-static broadphase.

---

### Step 6.3: Incremental BVH Update

Handle static body addition/removal without full rebuild.

---

### Step 6.4: Phase 6 Integration Test + Benchmark

**Benchmarks:**
```c
// bench_dynamic_vs_static_10000_static_bodies
// Compare grid-only vs BVH+grid
```

---

## Phase 7: Advanced Stability

**Goal:** Production-quality stability.

### Step 7.1: Manifold Point Reduction

Keep best 4 points using deepest + most spread algorithm.

---

### Step 7.2: Speculative Contacts

Generate contacts for close-but-not-touching pairs to prevent tunneling.

---

### Step 7.3: Position-Level Solve (Split Impulse)

Separate velocity and position correction for cleaner stacking.

---

### Step 7.4: Phase 7 Integration Test

**Test Cases:**
```c
// test_10_sphere_stack_1000_frames
// test_box_tower_20_high
// test_fast_object_no_tunnel
```

---

## Phase 8: Extension - Joints

**Goal:** Non-contact constraints (joints).

### Step 8.1: Joint Structure

**Files:**
- `include/ferrum/physics/joint.h`
- `src/physics/constraint/joint_distance.c`
- `src/physics/constraint/joint_hinge.c`
- `src/physics/constraint/joint_ball.c`

**Types:**
- Distance joint (spring-damper)
- Hinge joint (1 DOF rotation)
- Ball joint (3 DOF rotation)

---

### Step 8.2: Joint Constraint Build

Joints generate constraint rows alongside contacts in Stage 9.

---

### Step 8.3: Joint in Island Build

Joints contribute to island connectivity.

---

### Step 8.4: Phase 8 Integration Test

**Test Cases:**
```c
// test_pendulum_swings
// test_chain_of_bodies
// test_door_hinge
```

---

## Phase 9: Extension - Mesh Colliders

**Goal:** Triangle mesh collision for static geometry.

### Step 9.1: Mesh Collider Structure

BVH over triangles.

---

### Step 9.2: Primitive-vs-Mesh Narrowphase

Sphere/box/capsule vs triangle.

---

### Step 9.3: Phase 9 Integration Test

**Test Cases:**
```c
// test_sphere_on_terrain_mesh
// test_box_slides_down_ramp
```

---

## File Structure Appendix

```
include/ferrum/physics/
├── phys_types.h
├── body.h
├── collider.h
├── aabb.h
├── phys_pool.h
├── tier_list.h
├── spatial_grid.h
├── manifold.h
├── manifold_cache.h
├── constraint.h
├── island.h
├── world.h
├── step_plan.h
├── tier_classify.h
├── spatial_update.h
├── halo_closure.h
├── aabb_update.h
├── broadphase.h
├── narrowphase.h
├── manifold_build.h
├── stabilization.h
├── constraint_build.h
├── island_build.h
├── tgs_solve.h
├── integrate.h
├── cache_commit.h
├── snapshot.h
├── raycast.h
├── phys_jobs.h
└── joint.h

src/physics/
├── body/
│   └── body.c
├── collider/
│   └── collider.c
├── memory/
│   ├── phys_pool.c
│   └── phys_arena.c
├── tier/
│   └── tier_list.c
├── broadphase/
│   ├── spatial_grid.c
│   └── static_bvh.c
├── collision/
│   ├── aabb.c
│   ├── narrowphase_sphere.c
│   ├── narrowphase_box.c
│   ├── narrowphase_capsule.c
│   ├── narrowphase_sphere_box.c
│   ├── narrowphase_sphere_capsule.c
│   ├── narrowphase_box_capsule.c
│   ├── manifold.c
│   └── manifold_cache.c
├── solver/
│   ├── constraint.c
│   ├── island.c
│   └── tgs_solve.c
├── stages/
│   ├── step_plan.c
│   ├── tier_classify.c
│   ├── spatial_update.c
│   ├── halo_closure.c
│   ├── aabb_update.c
│   ├── broadphase.c
│   ├── narrowphase.c
│   ├── manifold_build.c
│   ├── stabilization.c
│   ├── constraint_build.c
│   ├── island_build.c
│   ├── integrate.c
│   └── cache_commit.c
├── world/
│   ├── world.c
│   └── tick.c
├── net/
│   ├── snapshot_encode.c
│   └── snapshot_decode.c
├── query/
│   └── raycast.c
├── jobs/
│   └── phys_job_dispatch.c
└── constraint/
    ├── joint_distance.c
    ├── joint_hinge.c
    └── joint_ball.c

tests/physics/
├── body_tests.c
├── collider_tests.c
├── aabb_tests.c
├── phys_pool_tests.c
├── tier_list_tests.c
├── spatial_grid_tests.c
├── manifold_tests.c
├── manifold_cache_tests.c
├── constraint_tests.c
├── island_tests.c
├── world_tests.c
├── step_plan_tests.c
├── tier_classify_tests.c
├── spatial_update_tests.c
├── halo_closure_tests.c
├── broadphase_tests.c
├── narrowphase_tests.c
├── narrowphase_sphere_box_tests.c
├── narrowphase_box_box_tests.c
├── ... (all narrowphase pairs)
├── manifold_build_tests.c
├── stabilization_tests.c
├── constraint_build_tests.c
├── island_build_tests.c
├── tgs_solve_tests.c
├── integrate_tests.c
├── cache_commit_tests.c
├── snapshot_tests.c
├── raycast_tests.c
├── phase0_integration_tests.c
├── phase1_integration_tests.c
├── phase1_bench.c
├── phase2_integration_tests.c
├── phase2_bench.c
├── phase3_integration_tests.c
├── phase3_bench.c
└── ... (subsequent phases)
```

---

## Performance Targets Summary

| Scenario | Target |
|----------|--------|
| 100 bodies, 30 Hz, 2 substeps | < 1.5 ms/tick |
| 1000 bodies, 30 Hz, 2 substeps | < 10 ms/tick |
| 5000 bodies, tiered, 8 threads | < 8 ms/tick |
| Broadphase 10k AABBs | < 2 ms |
| Narrowphase 1k pairs | < 1 ms |
| Solve 1k constraints, 8 iter | < 3 ms |
| Snapshot encode 100 bodies | < 50 µs |
| Snapshot delta 100 bodies | < 20 µs |
| Raycast 1000 casts, 1000 bodies | < 5 ms |

---

## Network Integration Checklist

Each phase must pass these network tests before completion:

- [ ] Full snapshot encode/decode round-trips correctly
- [ ] Delta compression works for changed bodies only
- [ ] Quantization error within acceptable bounds (< 1mm position, < 0.1° rotation)
- [ ] Sleeping bodies replicated correctly
- [ ] Tier state replicated correctly
- [ ] Snapshot size within budget (< 100 bytes/body full, < 20 bytes/body delta)
- [ ] Encode/decode performance within targets
