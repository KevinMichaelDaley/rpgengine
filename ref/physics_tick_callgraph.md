# Physics Tick Call Graph and Execution Flow

This document provides a complete staged ASCII diagram of the call graph and execution
flow for a single physics tick. It shows:
- The complete call stack hierarchy
- Job vs phase vs synchronization point distinctions
- Input/output arguments for each function
- Return values and data flow connections
- Full function signatures

---

## Legend

```
┌─────────────────────────────────────────────────────────────────────────────┐
│ LEGEND                                                                      │
├─────────────────────────────────────────────────────────────────────────────┤
│ ═══════  Top-level entry point / tick boundary                              │
│ ───────  Synchronous call (caller waits for return)                         │
│ ·······  Job dispatch (async, returns immediately, syncs on counter)        │
│ ┄┄┄┄┄┄┄  Data flow (output becomes input to next stage)                     │
│                                                                             │
│ [SYNC]      Single-threaded synchronous execution                           │
│ [PARALLEL]  Multiple parallel jobs                                          │
│ [DISPATCH]  Job dispatch call (non-blocking)                                │
│ [BARRIER]   Synchronization point (wait on counter)                         │
│                                                                             │
│ ──▶       Function call                                                     │
│ ══▶       Data dependency (output flows to next stage input)                │
│ ◆         Counter/barrier sync point                                        │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## Data Structures Overview

```
═══════════════════════════════════════════════════════════════════════════════
PERSISTENT STATE (lives across ticks)
═══════════════════════════════════════════════════════════════════════════════

phys_world_t                     Main world container
├── phys_body_pool_t             Double-buffered body storage
│   ├── bodies_curr: phys_body_t*    Read buffer (current tick input)
│   └── bodies_next: phys_body_t*    Write buffer (integration output)
├── collider_pool: pool_t        Shape reference per body
├── sphere_pool: pool_t          Shape data (radius)
├── box_pool: pool_t             Shape data (half_extents)
├── capsule_pool: pool_t         Shape data (radius, half_height)
├── aabbs: phys_aabb_t*          Bounding boxes (1:1 with bodies)
├── manifold_cache: phys_manifold_cache_t
│                                Warmstart data, persists across frames
├── spatial_grid: phys_spatial_grid_t
│                                Hash grid for broadphase
└── frame_arena: phys_frame_arena_t
                                 Per-tick transient allocations

═══════════════════════════════════════════════════════════════════════════════
TRANSIENT STATE (per-tick, arena-allocated)
═══════════════════════════════════════════════════════════════════════════════

phys_tier_lists_t                Index arrays per tier [T0..T5]
phys_collision_pair_t[]          AABB overlap pairs from broadphase
phys_contact_candidate_t[]       Raw contacts from narrowphase
phys_manifold_t[]                Merged manifolds (≤4 points each)
phys_stab_hint_t[]               Friction/restitution modifiers
phys_constraint_t[]              Jacobian rows for solver
phys_island_list_t               Connected components for parallel solve
phys_velocity_t[]                Solved velocity deltas
phys_impact_event_t[]            Gameplay events (damage, sound)
```

---

## Complete Call Graph

```
═══════════════════════════════════════════════════════════════════════════════
                              PHYSICS TICK ENTRY
═══════════════════════════════════════════════════════════════════════════════

void phys_world_tick(phys_world_t *world)
│
│   ARGUMENTS:
│   └── world: phys_world_t*          [IN/OUT] physics world state
│
│   RETURNS: void
│
├──────────────────────────────────────────────────────────────────────────────
│ TICK PROLOGUE [SYNC]
├──────────────────────────────────────────────────────────────────────────────
│
├──▶ phys_frame_arena_reset(&world->frame_arena)
│    │
│    │   void phys_frame_arena_reset(phys_frame_arena_t *arena)
│    │   ├── arena: phys_frame_arena_t*   [IN/OUT] frame allocator
│    │   └── RETURNS: void
│    │
│    │   EFFECT: Resets arena offset to 0. O(1).
│    │           All previous allocations invalidated.
│    │
│    └── OUTPUT: Empty arena ready for this tick's allocations
│
├──────────────────────────────────────────────────────────────────────────────
│ STAGE 0: STEP PLAN [SYNC] — ~10 µs
├──────────────────────────────────────────────────────────────────────────────
│
├──▶ phys_stage_step_plan(&plan, world, game_state)
│    │
│    │   void phys_stage_step_plan(
│    │       phys_step_plan_t *plan,              [OUT] computed plan
│    │       const phys_world_t *world,           [IN]  world config
│    │       const phys_game_state_t *game        [IN]  player state (nullable)
│    │   )
│    │
│    │   READS:
│    │   ├── world->config.substeps
│    │   ├── world->config.solver_iterations
│    │   ├── world->config.fixed_dt
│    │   └── game->manipulation_flags (if present)
│    │
│    │   WRITES:
│    │   └── plan->substeps, plan->solver_iterations, plan->dt,
│    │       plan->tier_params[0..5]
│    │
│    └── OUTPUT ═══════════════════════════════════════════════════════════════▶
│                phys_step_plan_t {
│                    substeps: uint32_t,        // e.g., 2
│                    solver_iterations: uint32_t, // e.g., 8
│                    dt: float,                 // e.g., 1/30 / 2 = 0.0167
│                    tier_params[6]: { active, iterations }
│                }
│
├──────────────────────────────────────────────────────────────────────────────
│ STAGE 1: BASE TIER CLASSIFICATION [PARALLEL] — 20-120 µs
├──────────────────────────────────────────────────────────────────────────────
│
├──▶ phys_stage_tier_classify(&tier_classify_args)
│    │
│    │   void phys_stage_tier_classify(const phys_tier_classify_args_t *args)
│    │
│    │   typedef struct phys_tier_classify_args_t {
│    │       const phys_body_t *bodies;           [IN]  body positions
│    │       uint32_t body_count;                 [IN]  active body count
│    │       const phys_vec3_t *player_positions; [IN]  player locations
│    │       uint32_t player_count;               [IN]  number of players
│    │       phys_tier_lists_t *tier_lists_out;   [OUT] classified indices
│    │       phys_frame_arena_t *arena;           [IN]  allocator for lists
│    │   }
│    │
│    │   ALGORITHM:
│    │   ├── For each body in parallel (1024 bodies/job):
│    │   │   ├── Compute distance to nearest player
│    │   │   ├── Check manipulation flags
│    │   │   ├── Apply hysteresis (keep tier for K frames unless trigger)
│    │   │   └── Classify into T0..T5 based on distance + flags
│    │   └── Atomic append to tier lists (thread-safe)
│    │
│    │   PARALLELISM (future):
│    │   ├── job_dispatch(tier_classify_job, args, PRIORITY_HIGH, &tier_done)
│    │   └── job_wait_counter(&tier_done, 0)
│    │
│    └── OUTPUT ═══════════════════════════════════════════════════════════════▶
│                phys_tier_lists_t {
│                    tiers[0]: { indices[], count } // T0: direct manipulation
│                    tiers[1]: { indices[], count } // T1: near interactive
│                    tiers[2]: { indices[], count } // T2: visible/hazardous
│                    tiers[3]: { indices[], count } // T3: world-shaping
│                    tiers[4]: { indices[], count } // T4: background dynamic
│                    tiers[5]: { indices[], count } // T5: sleeping/dormant
│                }
│
├──────────────────────────────────────────────────────────────────────────────
│ STAGE 2: SPATIAL INDEX UPDATE [PARALLEL] — 20-80 µs
├──────────────────────────────────────────────────────────────────────────────
│
├──▶ phys_stage_spatial_update(&spatial_update_args)
│    │
│    │   void phys_stage_spatial_update(const phys_spatial_update_args_t *args)
│    │
│    │   typedef struct phys_spatial_update_args_t {
│    │       const phys_body_t *bodies;           [IN]  positions/orientations
│    │       const phys_collider_t *colliders;    [IN]  shape references
│    │       const void *shape_pools[3];          [IN]  sphere/box/capsule data
│    │       phys_aabb_t *aabbs_out;              [OUT] computed AABBs
│    │       phys_spatial_grid_t *grid_out;       [OUT] populated grid
│    │       uint32_t body_count;                 [IN]  total bodies
│    │   }
│    │
│    │   CALLS:
│    │   ├──▶ phys_aabb_from_sphere(aabb, center, radius)     [per sphere body]
│    │   ├──▶ phys_aabb_from_box(aabb, center, rot, half_ext) [per box body]
│    │   ├──▶ phys_aabb_from_capsule(aabb, center, rot, r, h) [per capsule body]
│    │   │
│    │   │    void phys_aabb_from_sphere(
│    │   │        phys_aabb_t *aabb,      [OUT] computed bounding box
│    │   │        phys_vec3_t center,     [IN]  sphere center
│    │   │        float radius            [IN]  sphere radius
│    │   │    )
│    │   │
│    │   │    void phys_aabb_from_box(
│    │   │        phys_aabb_t *aabb,      [OUT] world-aligned bounding box
│    │   │        phys_vec3_t center,     [IN]  box center
│    │   │        phys_quat_t rotation,   [IN]  box orientation
│    │   │        phys_vec3_t half_ext    [IN]  local half-extents
│    │   │    )
│    │   │
│    │   │    void phys_aabb_from_capsule(
│    │   │        phys_aabb_t *aabb,      [OUT] world-aligned bounding box
│    │   │        phys_vec3_t center,     [IN]  capsule center
│    │   │        phys_quat_t rotation,   [IN]  capsule orientation
│    │   │        float radius,           [IN]  capsule radius
│    │   │        float half_height       [IN]  half-height of cylinder
│    │   │    )
│    │   │
│    │   └──▶ phys_spatial_grid_insert(grid, body_idx, &aabb)
│    │
│    │        void phys_spatial_grid_insert(
│    │            phys_spatial_grid_t *grid,  [IN/OUT] hash grid
│    │            uint32_t body_index,        [IN]     body ID
│    │            const phys_aabb_t *aabb     [IN]     body bounds
│    │        )
│    │
│    │        ALGORITHM:
│    │        ├── Compute min/max cell coordinates from AABB
│    │        ├── For each cell in range:
│    │        │   ├── Hash cell coords to bucket
│    │        │   └── Append body_index to bucket (arena-allocated)
│    │        └── Large AABBs may span multiple cells
│    │
│    └── OUTPUT ═══════════════════════════════════════════════════════════════▶
│                aabbs[]: phys_aabb_t            Updated bounding boxes
│                spatial_grid: phys_spatial_grid_t
│                    cells[hash]: { body_indices[], count }
│
│
╔══════════════════════════════════════════════════════════════════════════════╗
║                    SUBSTEP LOOP (repeat N times per tick)                    ║
║                    ──────────────────────────────────────                    ║
║                    for (substep = 0; substep < plan.substeps; ++substep)     ║
╚══════════════════════════════════════════════════════════════════════════════╝
│
├──────────────────────────────────────────────────────────────────────────────
│ STAGE 3: HALO CLOSURE [PARALLEL] — 20-120 µs
├──────────────────────────────────────────────────────────────────────────────
│
├──▶ phys_stage_halo_closure(&halo_args)
│    │
│    │   void phys_stage_halo_closure(const phys_halo_closure_args_t *args)
│    │
│    │   typedef struct phys_halo_closure_args_t {
│    │       const phys_body_t *bodies;           [IN]  velocities
│    │       const phys_aabb_t *aabbs;            [IN]  bounding boxes
│    │       const phys_spatial_grid_t *grid;     [IN]  spatial index
│    │       phys_tier_lists_t *tier_lists;       [IN/OUT] promote neighbors
│    │       float velocity_margin;               [IN]  expansion factor
│    │       float dt;                            [IN]  substep delta time
│    │   }
│    │
│    │   ALGORITHM:
│    │   ├── For each body in tier_lists[T0]:
│    │   │   ├── swept_aabb = expand(aabb, velocity * dt + margin)
│    │   │   ├── neighbors = grid_query(swept_aabb)
│    │   │   ├── For each neighbor not in T0:
│    │   │   │   └── Promote to T1 (add to tier_lists[1])
│    │   │   └── Update tier field on body
│    │   └── Apply hysteresis to prevent flapping
│    │
│    │   CALLS:
│    │   ├──▶ phys_aabb_expand(&swept, margin)
│    │   │    void phys_aabb_expand(phys_aabb_t *aabb, float margin)
│    │   │
│    │   └──▶ phys_spatial_grid_query(grid, &swept, out_indices, max, &count)
│    │        void phys_spatial_grid_query(
│    │            const phys_spatial_grid_t *grid,  [IN]  spatial index
│    │            const phys_aabb_t *aabb,          [IN]  query bounds
│    │            uint32_t *out_indices,            [OUT] matching body IDs
│    │            uint32_t max_results,             [IN]  buffer size
│    │            uint32_t *out_count               [OUT] actual count
│    │        )
│    │
│    └── OUTPUT ═══════════════════════════════════════════════════════════════▶
│                tier_lists: Modified, T0..T1 expanded with promoted neighbors
│
├──────────────────────────────────────────────────────────────────────────────
│ STAGE 4: AABB UPDATE (ACTIVE SET) [PARALLEL] — 20-80 µs
├──────────────────────────────────────────────────────────────────────────────
│
├──▶ phys_stage_aabb_update(&aabb_update_args)
│    │
│    │   void phys_stage_aabb_update(const phys_aabb_update_args_t *args)
│    │
│    │   typedef struct phys_aabb_update_args_t {
│    │       const phys_body_t *bodies;           [IN]  positions
│    │       const phys_collider_t *colliders;    [IN]  shapes
│    │       const void *shape_pools[3];          [IN]  shape data
│    │       phys_aabb_t *aabbs_out;              [OUT] updated AABBs
│    │       const phys_tier_lists_t *tier_lists; [IN]  active tiers only
│    │   }
│    │
│    │   ALGORITHM:
│    │   ├── For each tier T0..T4 (skip T5 sleeping):
│    │   │   └── For each body in tier:
│    │   │       └── Recompute AABB from current position + collider
│    │   └── Sleeping bodies (T5) retain stale AABBs
│    │
│    └── OUTPUT ═══════════════════════════════════════════════════════════════▶
│                aabbs[]: Updated for active bodies only
│
├──────────────────────────────────────────────────────────────────────────────
│ STAGE 5: BROADPHASE [PARALLEL] — 40-200 µs
├──────────────────────────────────────────────────────────────────────────────
│
├──▶ phys_stage_broadphase(&broadphase_args)
│    │
│    │   void phys_stage_broadphase(const phys_broadphase_args_t *args)
│    │
│    │   typedef struct phys_broadphase_args_t {
│    │       const phys_aabb_t *aabbs;            [IN]  bounding boxes
│    │       const phys_spatial_grid_t *grid;     [IN]  spatial index
│    │       phys_collision_pair_t *pairs_out;    [OUT] overlapping pairs
│    │       uint32_t max_pairs;                  [IN]  output buffer size
│    │       uint32_t *pair_count_out;            [OUT] actual pair count
│    │       phys_frame_arena_t *arena;           [IN]  for temp allocations
│    │   }
│    │
│    │   typedef struct phys_collision_pair_t {
│    │       uint32_t body_a;  // always < body_b
│    │       uint32_t body_b;
│    │   }
│    │
│    │   ALGORITHM:
│    │   ├── For each non-empty grid cell:
│    │   │   ├── For each unique pair (i, j) where i < j in cell:
│    │   │   │   ├── if phys_aabb_overlap(&aabbs[i], &aabbs[j]):
│    │   │   │   │   └── Append pair to output
│    │   │   │   └── Continue
│    │   │   └── Continue
│    │   ├── Deduplicate pairs (bodies may be in multiple cells)
│    │   │   └── Hash set or sort-unique
│    │   └── Future: BVH queries for static geometry
│    │
│    │   CALLS:
│    │   └──▶ phys_aabb_overlap(&a, &b) -> bool
│    │        bool phys_aabb_overlap(
│    │            const phys_aabb_t *a,  [IN] first box
│    │            const phys_aabb_t *b   [IN] second box
│    │        )
│    │        RETURNS: true if boxes overlap on all 3 axes
│    │
│    └── OUTPUT ═══════════════════════════════════════════════════════════════▶
│                pairs[]: phys_collision_pair_t  Deduplicated, sorted pairs
│                pair_count: uint32_t            Number of pairs
│
├──────────────────────────────────────────────────────────────────────────────
│ STAGE 6: NARROWPHASE [PARALLEL] — 80-350 µs
├──────────────────────────────────────────────────────────────────────────────
│
├──▶ phys_stage_narrowphase(&narrowphase_args)
│    │
│    │   void phys_stage_narrowphase(const phys_narrowphase_args_t *args)
│    │
│    │   typedef struct phys_narrowphase_args_t {
│    │       const phys_body_t *bodies;           [IN]  positions/orientations
│    │       const phys_collider_t *colliders;    [IN]  shape references
│    │       const void *shape_pools[3];          [IN]  shape data
│    │       const phys_collision_pair_t *pairs;  [IN]  from broadphase
│    │       uint32_t pair_count;                 [IN]  number of pairs
│    │       phys_contact_candidate_t *candidates_out; [OUT] contacts
│    │       uint32_t *candidate_count_out;       [OUT] contact count
│    │       uint32_t max_candidates;             [IN]  buffer size
│    │   }
│    │
│    │   typedef struct phys_contact_candidate_t {
│    │       uint32_t body_a;
│    │       uint32_t body_b;
│    │       phys_contact_point_t contacts[4];
│    │       uint8_t contact_count;
│    │   }
│    │
│    │   typedef struct phys_contact_point_t {
│    │       phys_vec3_t point_world;   // world-space contact point
│    │       phys_vec3_t normal;        // points from A to B
│    │       float penetration;         // positive = overlap
│    │       uint32_t feature_id;       // for persistent tracking
│    │   }
│    │
│    │   ALGORITHM:
│    │   ├── For each pair in parallel (64 pairs/job):
│    │   │   ├── Lookup collider types for body_a, body_b
│    │   │   ├── Dispatch to appropriate primitive test:
│    │   │   │   ├── sphere-sphere
│    │   │   │   ├── sphere-box
│    │   │   │   ├── sphere-capsule
│    │   │   │   ├── box-box
│    │   │   │   ├── box-capsule
│    │   │   │   └── capsule-capsule
│    │   │   ├── If contact found:
│    │   │   │   └── Append contact_candidate to output
│    │   │   └── Continue
│    │   └── Return contact candidates
│    │
│    │   PRIMITIVE TEST CALLS:
│    │   │
│    │   ├──▶ phys_sphere_vs_sphere(a, pos_a, b, pos_b, &contact) -> bool
│    │   │    bool phys_sphere_vs_sphere(
│    │   │        const phys_sphere_t *a,       [IN]  sphere A data
│    │   │        phys_vec3_t pos_a,            [IN]  sphere A position
│    │   │        const phys_sphere_t *b,       [IN]  sphere B data
│    │   │        phys_vec3_t pos_b,            [IN]  sphere B position
│    │   │        phys_contact_point_t *out     [OUT] contact if found
│    │   │    )
│    │   │    RETURNS: true if spheres overlap
│    │   │
│    │   ├──▶ phys_box_vs_box(a, pos_a, rot_a, b, pos_b, rot_b, out[], &count) -> bool
│    │   │    bool phys_box_vs_box(
│    │   │        const phys_box_t *a,          [IN]  box A half-extents
│    │   │        phys_vec3_t pos_a,            [IN]  box A position
│    │   │        phys_quat_t rot_a,            [IN]  box A orientation
│    │   │        const phys_box_t *b,          [IN]  box B half-extents
│    │   │        phys_vec3_t pos_b,            [IN]  box B position
│    │   │        phys_quat_t rot_b,            [IN]  box B orientation
│    │   │        phys_contact_point_t out[],   [OUT] up to 4 contacts
│    │   │        uint8_t *count                [OUT] actual contact count
│    │   │    )
│    │   │    RETURNS: true if boxes overlap (uses SAT + contact clipping)
│    │   │
│    │   ├──▶ phys_capsule_vs_capsule(a, pos_a, rot_a, b, pos_b, rot_b, &out) -> bool
│    │   │    bool phys_capsule_vs_capsule(
│    │   │        const phys_capsule_t *a,      [IN]  capsule A
│    │   │        phys_vec3_t pos_a,            [IN]  capsule A position
│    │   │        phys_quat_t rot_a,            [IN]  capsule A orientation
│    │   │        const phys_capsule_t *b,      [IN]  capsule B
│    │   │        phys_vec3_t pos_b,            [IN]  capsule B position
│    │   │        phys_quat_t rot_b,            [IN]  capsule B orientation
│    │   │        phys_contact_point_t *out     [OUT] contact if found
│    │   │    )
│    │   │    RETURNS: true if capsules overlap (closest points on segments)
│    │   │
│    │   ├──▶ phys_sphere_vs_box(...) -> bool
│    │   ├──▶ phys_sphere_vs_capsule(...) -> bool
│    │   └──▶ phys_box_vs_capsule(...) -> bool
│    │
│    └── OUTPUT ═══════════════════════════════════════════════════════════════▶
│                candidates[]: phys_contact_candidate_t
│                    Raw contacts with world-space points and normals
│                candidate_count: uint32_t
│
├──────────────────────────────────────────────────────────────────────────────
│ STAGE 7: MANIFOLD BUILD + CACHE MERGE [PARALLEL] — 25-120 µs
├──────────────────────────────────────────────────────────────────────────────
│
├──▶ phys_stage_manifold_build(&manifold_build_args)
│    │
│    │   void phys_stage_manifold_build(const phys_manifold_build_args_t *args)
│    │
│    │   typedef struct phys_manifold_build_args_t {
│    │       const phys_contact_candidate_t *candidates; [IN]  raw contacts
│    │       uint32_t candidate_count;                   [IN]  count
│    │       phys_manifold_cache_t *cache;               [IN/OUT] persistent
│    │       phys_manifold_t *manifolds_out;             [OUT] this frame
│    │       uint32_t *manifold_count_out;               [OUT] count
│    │       uint32_t max_manifolds;                     [IN]  buffer size
│    │       phys_frame_arena_t *arena;                  [IN]  allocator
│    │   }
│    │
│    │   typedef struct phys_manifold_t {
│    │       uint32_t body_a;
│    │       uint32_t body_b;
│    │       uint8_t point_count;
│    │       phys_contact_point_t points[4];
│    │       float friction;
│    │       float restitution;
│    │       float normal_impulse_sum[4];      // warmstart
│    │       float tangent_impulse_sum[4][2];  // warmstart
│    │   }
│    │
│    │   ALGORITHM:
│    │   ├── For each candidate:
│    │   │   ├── pair_key = sort(body_a, body_b)
│    │   │   ├── cached = phys_manifold_cache_find(cache, body_a, body_b)
│    │   │   ├── If cached exists:
│    │   │   │   ├── Match new contacts to cached by feature_id
│    │   │   │   ├── Preserve warmstart impulses for matched contacts
│    │   │   │   └── Merge new + cached contacts
│    │   │   ├── phys_manifold_reduce(&manifold) // keep best ≤4 points
│    │   │   └── Output manifold to manifolds_out
│    │   └── Update cache with new manifolds
│    │
│    │   CALLS:
│    │   ├──▶ phys_manifold_cache_find(cache, body_a, body_b) -> phys_manifold_t*
│    │   │    phys_manifold_t *phys_manifold_cache_find(
│    │   │        phys_manifold_cache_t *cache,  [IN]  cache table
│    │   │        uint32_t body_a,               [IN]  first body
│    │   │        uint32_t body_b                [IN]  second body
│    │   │    )
│    │   │    RETURNS: cached manifold or NULL (O(1) average lookup)
│    │   │
│    │   ├──▶ phys_manifold_reduce(&manifold)
│    │   │    void phys_manifold_reduce(phys_manifold_t *m)
│    │   │    EFFECT: Reduces to ≤4 points with best contact coverage
│    │   │
│    │   └──▶ phys_manifold_cache_insert(cache, body_a, body_b) -> phys_manifold_t*
│    │        phys_manifold_t *phys_manifold_cache_insert(
│    │            phys_manifold_cache_t *cache,  [IN/OUT] cache table
│    │            uint32_t body_a,               [IN]  first body
│    │            uint32_t body_b                [IN]  second body
│    │        )
│    │        RETURNS: newly inserted (or existing) cache entry
│    │
│    └── OUTPUT ═══════════════════════════════════════════════════════════════▶
│                manifolds[]: phys_manifold_t
│                    Merged manifolds with warmstart data
│                manifold_count: uint32_t
│                cache: Updated with new/refreshed entries
│
├──────────────────────────────────────────────────────────────────────────────
│ STAGE 8: STABILIZATION HINTS [PARALLEL] — 15-120 µs
├──────────────────────────────────────────────────────────────────────────────
│
├──▶ phys_stage_stabilization(&stab_args)
│    │
│    │   void phys_stage_stabilization(const phys_stabilization_args_t *args)
│    │
│    │   typedef struct phys_stabilization_args_t {
│    │       const phys_manifold_t *manifolds;    [IN]  contact manifolds
│    │       uint32_t manifold_count;             [IN]  count
│    │       const phys_body_t *bodies;           [IN]  velocities
│    │       const phys_tier_lists_t *tier_lists; [IN]  tier info
│    │       phys_stab_hint_t *hints_out;         [OUT] per-manifold hints
│    │   }
│    │
│    │   typedef struct phys_stab_hint_t {
│    │       float friction_scale;     // 1.0 = normal, 3.0 = resting boost
│    │       float restitution_scale;  // 1.0 = normal, 0.0 = suppress bounce
│    │   }
│    │
│    │   ALGORITHM:
│    │   ├── For each manifold:
│    │   │   ├── Compute relative velocity at contact point:
│    │   │   │   v_rel = (v_a + ω_a × r_a) - (v_b + ω_b × r_b)
│    │   │   ├── v_normal = dot(v_rel, normal)
│    │   │   ├── If |v_normal| < resting_threshold:
│    │   │   │   ├── friction_scale = 3.0  (boost friction for stability)
│    │   │   │   └── restitution_scale = 0.0  (suppress bounce)
│    │   │   ├── Else if v_normal > 0:  (separating)
│    │   │   │   ├── friction_scale = 1.0
│    │   │   │   └── restitution_scale = 1.0
│    │   │   └── Else:  (approaching)
│    │   │       ├── friction_scale = 1.0
│    │   │       └── restitution_scale = 1.0
│    │   └── Tier affects thresholds (T0 more aggressive)
│    │
│    └── OUTPUT ═══════════════════════════════════════════════════════════════▶
│                hints[]: phys_stab_hint_t
│                    Per-manifold friction/restitution modifiers
│
├──────────────────────────────────────────────────────────────────────────────
│ STAGE 9: CONSTRAINT BUILD [PARALLEL] — ~100 µs
├──────────────────────────────────────────────────────────────────────────────
│
├──▶ phys_stage_constraint_build(&constraint_build_args)
│    │
│    │   void phys_stage_constraint_build(const phys_constraint_build_args_t *args)
│    │
│    │   typedef struct phys_constraint_build_args_t {
│    │       const phys_manifold_t *manifolds;    [IN]  contact manifolds
│    │       const phys_stab_hint_t *hints;       [IN]  stabilization mods
│    │       uint32_t manifold_count;             [IN]  count
│    │       const phys_body_t *bodies;           [IN]  mass/inertia
│    │       phys_constraint_t *constraints_out;  [OUT] Jacobian rows
│    │       uint32_t *constraint_count_out;      [OUT] count
│    │       uint32_t max_constraints;            [IN]  buffer size
│    │       float dt;                            [IN]  timestep
│    │       float baumgarte;                     [IN]  penetration correction
│    │   }
│    │
│    │   typedef struct phys_jacobian_row_t {
│    │       phys_vec3_t J_va;    // ∂C/∂v_a (linear)
│    │       phys_vec3_t J_wa;    // ∂C/∂ω_a (angular)
│    │       phys_vec3_t J_vb;    // ∂C/∂v_b (linear)
│    │       phys_vec3_t J_wb;    // ∂C/∂ω_b (angular)
│    │       float effective_mass; // 1 / (J M^-1 J^T)
│    │       float bias;           // velocity bias (baumgarte + restitution)
│    │       float lambda;         // accumulated impulse
│    │       float lambda_min;     // clamp min (0 for normal, -inf for friction)
│    │       float lambda_max;     // clamp max
│    │   }
│    │
│    │   typedef struct phys_constraint_t {
│    │       uint32_t body_a;
│    │       uint32_t body_b;
│    │       uint8_t row_count;   // 3 for contact (normal + 2 friction)
│    │       phys_jacobian_row_t rows[3];
│    │   }
│    │
│    │   ALGORITHM:
│    │   ├── For each manifold:
│    │   │   ├── For each contact point:
│    │   │   │   ├── Build normal constraint row:
│    │   │   │   │   ├── J_va = -normal
│    │   │   │   │   ├── J_wa = -(r_a × normal)
│    │   │   │   │   ├── J_vb = +normal
│    │   │   │   │   ├── J_wb = +(r_b × normal)
│    │   │   │   │   ├── effective_mass = 1 / (J M^-1 J^T)
│    │   │   │   │   ├── bias = baumgarte * penetration / dt
│    │   │   │   │       + restitution * hints[i].restitution_scale * v_rel_n
│    │   │   │   │   ├── lambda = cached_normal_impulse (warmstart)
│    │   │   │   │   ├── lambda_min = 0 (no pull)
│    │   │   │   │   └── lambda_max = +∞
│    │   │   │   ├── Build friction constraint row 1 (tangent1):
│    │   │   │   │   ├── tangent1 = orthogonal to normal
│    │   │   │   │   ├── J_va = -tangent1, J_wa = -(r_a × tangent1)
│    │   │   │   │   ├── J_vb = +tangent1, J_wb = +(r_b × tangent1)
│    │   │   │   │   ├── lambda = cached_tangent_impulse[0]
│    │   │   │   │   ├── lambda_min = -µ * λ_normal * hints[i].friction_scale
│    │   │   │   │   └── lambda_max = +µ * λ_normal * hints[i].friction_scale
│    │   │   │   └── Build friction constraint row 2 (tangent2):
│    │   │   │       └── (same pattern, orthogonal to both normal and tangent1)
│    │   │   └── Output constraint
│    │   └── Continue
│    │
│    │   CALLS:
│    │   └──▶ phys_constraint_build_contact(&constraint, body_a, body_b,
│    │                                       &contact, friction, restitution, dt)
│    │        void phys_constraint_build_contact(
│    │            phys_constraint_t *c,             [OUT] constraint to build
│    │            const phys_body_t *body_a,        [IN]  first body
│    │            const phys_body_t *body_b,        [IN]  second body
│    │            const phys_contact_point_t *pt,   [IN]  contact point
│    │            float friction,                   [IN]  combined friction
│    │            float restitution,                [IN]  combined restitution
│    │            float dt                          [IN]  timestep
│    │        )
│    │
│    └── OUTPUT ═══════════════════════════════════════════════════════════════▶
│                constraints[]: phys_constraint_t
│                    Jacobian rows with precomputed effective mass
│                    Warmstart lambdas loaded
│                constraint_count: uint32_t
│
├──────────────────────────────────────────────────────────────────────────────
│ STAGE 10: ISLAND BUILD (T0/T1 ONLY) [SYNC] — 20-150 µs
├──────────────────────────────────────────────────────────────────────────────
│
├──▶ phys_stage_island_build(&island_build_args)
│    │
│    │   void phys_stage_island_build(const phys_island_build_args_t *args)
│    │
│    │   typedef struct phys_island_build_args_t {
│    │       const phys_constraint_t *constraints;  [IN]  T0/T1 constraints only
│    │       uint32_t constraint_count;             [IN]  count (T0/T1)
│    │       uint32_t body_count;                   [IN]  total bodies
│    │       phys_island_list_t *islands_out;       [OUT] connected components
│    │       phys_frame_arena_t *arena;             [IN]  allocator
│    │   }
│    │
│    │   typedef struct phys_island_t {
│    │       uint32_t *body_indices;
│    │       uint32_t body_count;
│    │       uint32_t *constraint_indices;
│    │       uint32_t constraint_count;
│    │   }
│    │
│    │   typedef struct phys_island_list_t {
│    │       phys_island_t *islands;
│    │       uint32_t count;
│    │       // Union-find workspace
│    │       uint32_t *parent;
│    │       uint32_t *rank;
│    │   }
│    │
│    │   Only T0/T1 (TGS) constraints participate in island build.
│    │   T2–T4 constraints skip island build entirely — they go to XPBD.
│    │
│    │   ALGORITHM (Union-Find):
│    │   ├── Initialize: parent[i] = i for all bodies
│    │   ├── For each T0/T1 constraint:
│    │   │   └── union(body_a, body_b)
│    │   ├── Group bodies by root:
│    │   │   ├── For each body: find root
│    │   │   └── Bucket bodies by root
│    │   ├── Group constraints by root:
│    │   │   └── Constraint goes to island of find(body_a)
│    │   └── Output island structures
│    │
│    │   CALLS:
│    │   ├──▶ uf_find(parent, i) -> root
│    │   │    uint32_t uf_find(uint32_t *parent, uint32_t i)
│    │   │    // Path compression for O(α(n)) amortized
│    │   │
│    │   └──▶ uf_union(parent, rank, a, b)
│    │        void uf_union(uint32_t *parent, uint32_t *rank, uint32_t a, uint32_t b)
│    │        // Union by rank for O(α(n)) amortized
│    │
│    │   COMPLEXITY: O(n α(n)) where α is inverse Ackermann
│    │
│    └── OUTPUT ═══════════════════════════════════════════════════════════════▶
│                islands: phys_island_list_t
│                    Each island has lists of body indices and constraint indices
│                    Islands are independent (can be solved in parallel)
│                    Only T0/T1 bodies appear in islands
│
├──────────────────────────────────────────────────────────────────────────────
│ STAGE 11a: TGS SOLVE (T0/T1) [PARALLEL] — 100-400 µs
│ STAGE 11b: XPBD SOLVE (T2–T4) [PARALLEL, CONCURRENT WITH 11a] — 80-350 µs
├──────────────────────────────────────────────────────────────────────────────
│
│   Stages 11a and 11b run CONCURRENTLY on disjoint body sets:
│   • 11a: TGS (island-based, Gauss-Seidel) for near-field (T0/T1)
│   • 11b: Jacobi XPBD (per-body parallel) for far-field (T2–T4)
│
├──▶ phys_stage_tgs_solve(&tgs_solve_args)                  [Stage 11a]
│    │
│    │   void phys_stage_tgs_solve(const phys_tgs_solve_args_t *args)
│    │
│    │   typedef struct phys_tgs_solve_args_t {
│    │       const phys_island_list_t *islands;   [IN]  connected components (T0/T1)
│    │       phys_constraint_t *constraints;      [IN/OUT] lambda updated
│    │       const phys_body_t *bodies_in;        [IN]  current velocities
│    │       phys_velocity_t *velocities_out;     [OUT] delta velocities
│    │       uint32_t body_count;                 [IN]  total bodies
│    │       uint32_t iterations;                 [IN]  solver iterations (20-24)
│    │   }
│    │
│    │   typedef struct phys_velocity_t {
│    │       phys_vec3_t linear;
│    │       phys_vec3_t angular;
│    │   }
│    │
│    │   ALGORITHM (per island, parallelizable):
│    │   │
│    │   │   // Initialize velocities from input
│    │   ├── For each body in island:
│    │   │   └── velocities[body] = bodies_in[body].{linear_vel, angular_vel}
│    │   │
│    │   │   // TGS iteration loop
│    │   ├── For iter in 0..iterations:
│    │   │   ├── For each constraint in island:
│    │   │   │   ├── For each row in constraint:
│    │   │   │   │   │
│    │   │   │   │   │   // Compute constraint velocity
│    │   │   │   │   ├── v_constraint = J · v
│    │   │   │   │   │     = J_va · v_a + J_wa · ω_a + J_vb · v_b + J_wb · ω_b
│    │   │   │   │   │
│    │   │   │   │   │   // Compute impulse delta
│    │   │   │   │   ├── Δλ = (bias - v_constraint) / effective_mass
│    │   │   │   │   │
│    │   │   │   │   │   // Clamp accumulated impulse
│    │   │   │   │   ├── λ_old = row.lambda
│    │   │   │   │   ├── λ_new = clamp(λ_old + Δλ, lambda_min, lambda_max)
│    │   │   │   │   ├── Δλ = λ_new - λ_old
│    │   │   │   │   ├── row.lambda = λ_new
│    │   │   │   │   │
│    │   │   │   │   │   // Apply impulse to velocities (Gauss-Seidel: immediate)
│    │   │   │   │   ├── v_a += inv_mass_a * J_va * Δλ
│    │   │   │   │   ├── ω_a += inv_inertia_a * J_wa * Δλ
│    │   │   │   │   ├── v_b += inv_mass_b * J_vb * Δλ
│    │   │   │   │   └── ω_b += inv_inertia_b * J_wb * Δλ
│    │   │   │   └── Continue to next row
│    │   │   └── Continue to next constraint
│    │   └── Continue to next iteration
│    │   │
│    │   │   // Write final velocities
│    │   └── For each body in island:
│    │       └── velocities_out[body] = velocities[body]
│    │
│    │   PARALLELISM:
│    │   ├── Large islands: 1 job per island
│    │   ├── Small islands: batch multiple into 1 job
│    │   └── No synchronization needed (islands are independent)
│    │
│    └── OUTPUT ═══════════════════════════════════════════════════════════════▶
│                velocities_out[]: phys_velocity_t (T0/T1 bodies)
│                    Solved linear and angular velocities
│                constraints[]: lambda values updated for warmstarting
│
├──▶ phys_stage_xpbd_solve(&xpbd_solve_args)                [Stage 11b]
│    │
│    │   void phys_stage_xpbd_solve(const phys_xpbd_solve_args_t *args)
│    │
│    │   typedef struct phys_xpbd_solve_args_t {
│    │       phys_constraint_t *constraints;    [IN/OUT] lambda updated
│    │       uint32_t constraint_count;         [IN]     T2–T4 constraints
│    │       const phys_body_t *bodies_in;      [IN]     start-of-step positions
│    │       phys_body_t *bodies_out;           [OUT]    position corrections
│    │       phys_velocity_t *velocities_out;   [OUT]    derived velocity = Δx/dt
│    │       uint32_t body_count;               [IN]     T2–T4 body count
│    │       uint32_t iterations;               [IN]     4–8 typical
│    │       float omega;                       [IN]     Jacobi relaxation (0.5–0.8)
│    │       float dt;                          [IN]     substep dt
│    │   }
│    │
│    │   ALGORITHM (Jacobi XPBD, no island dependency):
│    │   │
│    │   │   // Copy initial positions
│    │   ├── For each T2–T4 body:
│    │   │   └── predicted_pos[body] = bodies_in[body].position
│    │   │
│    │   │   // XPBD iteration loop
│    │   ├── For iter in 0..iterations:
│    │   │   │
│    │   │   │   // Zero per-body correction accumulators
│    │   │   ├── For each body: Δx_accum[body] = 0
│    │   │   │
│    │   │   │   // Evaluate all constraints (Jacobi: read from current positions)
│    │   │   ├── For each constraint:
│    │   │   │   ├── C = evaluate_constraint(predicted_pos[body_a], predicted_pos[body_b])
│    │   │   │   ├── ∇C = constraint gradient
│    │   │   │   ├── α̃ = compliance / dt²
│    │   │   │   ├── Δλ = (-C - α̃ · λ) / (∇C^T M⁻¹ ∇C + α̃)
│    │   │   │   ├── λ += Δλ
│    │   │   │   ├── Δx_accum[body_a] += inv_mass_a * ∇C_a * Δλ
│    │   │   │   └── Δx_accum[body_b] += inv_mass_b * ∇C_b * Δλ
│    │   │   │
│    │   │   │   // Apply corrections with Jacobi relaxation
│    │   │   └── For each body:
│    │   │       └── predicted_pos[body] += ω * Δx_accum[body]
│    │   │
│    │   │   // Derive velocities from position change
│    │   └── For each body:
│    │       ├── velocities_out[body].linear = (predicted_pos - original_pos) / dt
│    │       ├── velocities_out[body].angular = quat_delta_to_angular_vel(...)
│    │       └── bodies_out[body].position = predicted_pos
│    │
│    │   PARALLELISM:
│    │   ├── Split bodies across jobs (128 bodies/job)
│    │   ├── Jacobi pattern: no order dependency within iteration
│    │   ├── Per-body corrections accumulated atomically or with double-buffering
│    │   └── Embarrassingly parallel — ideal for far-field with many scattered bodies
│    │
│    │   KEY DIFFERENCE FROM TGS:
│    │   ├── TGS: velocity-level, sequential within island, best convergence
│    │   ├── XPBD: position-level, parallel over bodies, unconditionally stable
│    │   └── XPBD is less accurate but acceptable for T2–T4 distances
│    │
│    └── OUTPUT ═══════════════════════════════════════════════════════════════▶
│                velocities_out[]: phys_velocity_t (T2–T4 bodies)
│                    Derived velocities from position corrections
│                bodies_out[]: corrected positions
│                constraints[]: lambda values updated for warmstarting
│
│   ┌────────────────────────────────────────────────────────────────────────┐
│   │ SOLVER TRANSITION (runs during constraint build, Stage 9):           │
│   │                                                                      │
│   │ When a body's tier crosses the T1↔T2 boundary:                      │
│   │                                                                      │
│   │ TGS → XPBD (demotion):  λ_xpbd = λ_impulse * dt                   │
│   │ XPBD → TGS (promotion): λ_impulse = clamp(λ_xpbd / dt, λ_min,max) │
│   │                                                                      │
│   │ void phys_solver_convert_tgs_to_xpbd(phys_constraint_t *c, float dt)│
│   │ void phys_solver_convert_xpbd_to_tgs(phys_constraint_t *c, float dt)│
│   └────────────────────────────────────────────────────────────────────────┘
│
├──────────────────────────────────────────────────────────────────────────────
│ STAGE 12: INTEGRATE + SLEEP [PARALLEL] — 20-120 µs
├──────────────────────────────────────────────────────────────────────────────
│
├──▶ phys_stage_integrate(&integrate_args)
│    │
│    │   void phys_stage_integrate(const phys_integrate_args_t *args)
│    │
│    │   typedef struct phys_integrate_args_t {
│    │       const phys_body_t *bodies_in;        [IN]  current state
│    │       const phys_velocity_t *velocities;   [IN]  solved velocities
│    │       phys_body_t *bodies_out;             [OUT] next state
│    │       uint32_t body_count;                 [IN]  total bodies
│    │       float dt;                            [IN]  substep delta time
│    │       phys_vec3_t gravity;                 [IN]  gravity vector
│    │       float sleep_threshold_linear;        [IN]  linear sleep threshold
│    │       float sleep_threshold_angular;       [IN]  angular sleep threshold
│    │       uint32_t sleep_delay_frames;         [IN]  frames before sleep
│    │   }
│    │
│    │   ALGORITHM (per body, parallelizable):
│    │   │
│    │   ├── If body is static:
│    │   │   └── bodies_out[i] = bodies_in[i]  // no change
│    │   │
│    │   ├── Else (dynamic body):
│    │   │   │
│    │   │   │   // Apply gravity (if not already in solver)
│    │   │   ├── v = velocities[i].linear + gravity * dt
│    │   │   ├── ω = velocities[i].angular
│    │   │   │
│    │   │   │   // Semi-implicit Euler position integration
│    │   │   ├── p_new = bodies_in[i].position + v * dt
│    │   │   │
│    │   │   │   // Quaternion integration
│    │   │   ├── q_delta = quat(0, ω.x, ω.y, ω.z) * bodies_in[i].orientation
│    │   │   ├── q_new = bodies_in[i].orientation + 0.5 * q_delta * dt
│    │   │   ├── q_new = normalize(q_new)
│    │   │   │
│    │   │   │   // Write output
│    │   │   ├── bodies_out[i].position = p_new
│    │   │   ├── bodies_out[i].orientation = q_new
│    │   │   ├── bodies_out[i].linear_vel = v
│    │   │   └── bodies_out[i].angular_vel = ω
│    │   │
│    │   │   // Sleep detection
│    │   └── If |v| < threshold_linear && |ω| < threshold_angular:
│    │       ├── Increment sleep counter
│    │       ├── If sleep counter > sleep_delay_frames:
│    │       │   ├── bodies_out[i].flags |= PHYS_BODY_SLEEPING
│    │       │   └── Move to tier T5
│    │       └── Else: continue counting
│    │
│    └── OUTPUT ═══════════════════════════════════════════════════════════════▶
│                bodies_out[]: phys_body_t
│                    Updated positions, orientations, velocities
│                    Sleep flags set where appropriate
│
├──────────────────────────────────────────────────────────────────────────────
│ STAGE 13: CACHE COMMIT + EVENTS [SYNC] — ~20 µs
├──────────────────────────────────────────────────────────────────────────────
│
├──▶ phys_stage_cache_commit(&cache_commit_args)
│    │
│    │   void phys_stage_cache_commit(const phys_cache_commit_args_t *args)
│    │
│    │   typedef struct phys_cache_commit_args_t {
│    │       const phys_manifold_t *manifolds;    [IN]  current frame manifolds
│    │       const phys_constraint_t *constraints;[IN]  solved constraints
│    │       uint32_t manifold_count;             [IN]  count
│    │       phys_manifold_cache_t *cache;        [IN/OUT] persistent cache
│    │       phys_impact_event_t *events_out;     [OUT] gameplay events
│    │       uint32_t *event_count_out;           [OUT] event count
│    │       uint32_t max_events;                 [IN]  buffer size
│    │       float impact_threshold;              [IN]  min impulse for event
│    │   }
│    │
│    │   typedef struct phys_impact_event_t {
│    │       uint32_t body_a;
│    │       uint32_t body_b;
│    │       phys_vec3_t point;
│    │       phys_vec3_t normal;
│    │       float impulse_magnitude;
│    │   }
│    │
│    │   ALGORITHM:
│    │   ├── For each manifold:
│    │   │   ├── Find corresponding cache entry
│    │   │   ├── Copy solved lambdas to cache for warmstarting:
│    │   │   │   ├── cache.normal_impulse_sum[] = constraints.rows[0].lambda
│    │   │   │   └── cache.tangent_impulse_sum[][] = constraints.rows[1,2].lambda
│    │   │   ├── Reset cache age to 0
│    │   │   ├── If max(lambda[normal]) > impact_threshold:
│    │   │   │   └── Emit impact event
│    │   │   └── Continue
│    │   ├── Age non-touched cache entries
│    │   └── Expire entries older than max_age
│    │
│    │   CALLS:
│    │   └──▶ phys_manifold_cache_expire_old(cache, max_age)
│    │        void phys_manifold_cache_expire_old(
│    │            phys_manifold_cache_t *cache,  [IN/OUT] cache
│    │            uint32_t max_age               [IN]  frames until expiry
│    │        )
│    │
│    └── OUTPUT ═══════════════════════════════════════════════════════════════▶
│                cache: Updated with solved impulses (warmstart for next frame)
│                events_out[]: phys_impact_event_t
│                    Significant impacts for gameplay (sound, damage, fracture)
│
╔══════════════════════════════════════════════════════════════════════════════╗
║                           END SUBSTEP LOOP                                   ║
║                   Repeat stages 3-13 for remaining substeps                  ║
╚══════════════════════════════════════════════════════════════════════════════╝
│
├──────────────────────────────────────────────────────────────────────────────
│ STAGE 14: BUFFER SWAP [SYNC] — O(1)
├──────────────────────────────────────────────────────────────────────────────
│
├──▶ phys_body_pool_swap_buffers(&world->body_pool)
│    │
│    │   void phys_body_pool_swap_buffers(phys_body_pool_t *pool)
│    │
│    │   IMPLEMENTATION:
│    │   ├── phys_body_t *tmp = pool->bodies_curr
│    │   ├── pool->bodies_curr = pool->bodies_next
│    │   └── pool->bodies_next = tmp
│    │
│    │   EFFECT: Next tick reads from what was just written.
│    │           O(1) pointer swap, no data copy.
│    │
│    └── OUTPUT: bodies_curr now contains integrated state
│
├──────────────────────────────────────────────────────────────────────────────
│ TICK EPILOGUE [SYNC]
├──────────────────────────────────────────────────────────────────────────────
│
├──▶ world->tick_count++
│
└── RETURN (tick complete)

═══════════════════════════════════════════════════════════════════════════════
                              PHYSICS TICK END
═══════════════════════════════════════════════════════════════════════════════
```

---

## Parallel Job Execution Model (Phase 3+)

```
═══════════════════════════════════════════════════════════════════════════════
JOB-BASED EXECUTION (when parallelism enabled)
═══════════════════════════════════════════════════════════════════════════════

void phys_world_tick_parallel(phys_world_t *world, job_system_t *sys)
│
├──▶ phys_frame_arena_reset(&world->frame_arena)
│
├──▶ phys_stage_step_plan(&plan, ...)                        [SYNC - single]
│
│   ┌─────────────────────────────────────────────────────────────────────────
│   │ STAGE 1: TIER CLASSIFICATION [DISPATCH PARALLEL JOBS]
│   ├─────────────────────────────────────────────────────────────────────────
│   │
│   ├──▶ job_counter_init(&tier_done, 0)
│   ├··▶ job_dispatch(sys, tier_classify_job, &args[0..N-1], &tier_done)
│   │    │
│   │    │   N = ceil(body_count / 1024)  // 1024 bodies per job
│   │    │
│   │    │   void tier_classify_job(void *arg)
│   │    │   {
│   │    │       phys_tier_classify_job_args_t *a = arg;
│   │    │       for (i = a->start; i < a->end; ++i) {
│   │    │           tier = classify_body(a->bodies[i], a->players);
│   │    │           atomic_append(&a->tier_lists->tiers[tier], i);
│   │    │       }
│   │    │   }
│   │    │
│   │    └──▶ Returns immediately (job queued)
│   │
│   └──▶ job_wait_counter(&tier_done, 0) ◆────────── BARRIER
│
│   ┌─────────────────────────────────────────────────────────────────────────
│   │ STAGE 2: SPATIAL UPDATE [DISPATCH PARALLEL JOBS]
│   ├─────────────────────────────────────────────────────────────────────────
│   │
│   ├──▶ job_counter_init(&spatial_done, 0)
│   ├··▶ job_dispatch(sys, spatial_update_job, &args[0..N-1], &spatial_done)
│   │    │
│   │    │   N = ceil(body_count / 512)  // 512 bodies per job
│   │    │
│   │    │   void spatial_update_job(void *arg)
│   │    │   {
│   │    │       // Compute AABBs for assigned bodies
│   │    │       // Thread-local cell buffers, merge at end
│   │    │   }
│   │    │
│   │    └──▶ Returns immediately
│   │
│   └──▶ job_wait_counter(&spatial_done, 0) ◆────── BARRIER
│
│   ╔═════════════════════════════════════════════════════════════════════════╗
│   ║                           SUBSTEP LOOP                                  ║
│   ╚═════════════════════════════════════════════════════════════════════════╝
│   │
│   ├─── for (substep = 0; substep < plan.substeps; ++substep) ───────────────
│   │
│   │   ┌─────────────────────────────────────────────────────────────────────
│   │   │ STAGE 3: HALO CLOSURE [SMALL PARALLEL]
│   │   ├─────────────────────────────────────────────────────────────────────
│   │   ├··▶ job_dispatch(halo_job, ..., &halo_done)  // 1 per T0 body
│   │   └──▶ job_wait_counter(&halo_done, 0) ◆
│   │
│   │   ┌─────────────────────────────────────────────────────────────────────
│   │   │ STAGE 4: AABB UPDATE [PARALLEL]
│   │   ├─────────────────────────────────────────────────────────────────────
│   │   ├··▶ job_dispatch(aabb_job, ..., &aabb_done)
│   │   └──▶ job_wait_counter(&aabb_done, 0) ◆
│   │
│   │   ┌─────────────────────────────────────────────────────────────────────
│   │   │ STAGE 5: BROADPHASE [PARALLEL - cell partitioned]
│   │   ├─────────────────────────────────────────────────────────────────────
│   │   ├··▶ job_dispatch(broadphase_job, ..., &broad_done)
│   │   │    // Each job handles subset of grid cells
│   │   │    // Thread-local pair buffers, merge at end
│   │   └──▶ job_wait_counter(&broad_done, 0) ◆
│   │
│   │   ┌─────────────────────────────────────────────────────────────────────
│   │   │ STAGE 6: NARROWPHASE [PARALLEL - pair batched]
│   │   ├─────────────────────────────────────────────────────────────────────
│   │   ├··▶ job_dispatch(narrow_job, ..., &narrow_done)
│   │   │    // 64 pairs per job
│   │   └──▶ job_wait_counter(&narrow_done, 0) ◆
│   │
│   │   ┌─────────────────────────────────────────────────────────────────────
│   │   │ STAGE 7: MANIFOLD BUILD [PARALLEL]
│   │   ├─────────────────────────────────────────────────────────────────────
│   │   ├··▶ job_dispatch(manifold_job, ..., &manifold_done)
│   │   │    // 32 candidates per job
│   │   │    // Cache access needs synchronization
│   │   └──▶ job_wait_counter(&manifold_done, 0) ◆
│   │
│   │   ┌─────────────────────────────────────────────────────────────────────
│   │   │ STAGE 8: STABILIZATION [PARALLEL]
│   │   ├─────────────────────────────────────────────────────────────────────
│   │   ├··▶ job_dispatch(stab_job, ..., &stab_done)
│   │   │    // 64 manifolds per job
│   │   └──▶ job_wait_counter(&stab_done, 0) ◆
│   │
│   │   ┌─────────────────────────────────────────────────────────────────────
│   │   │ STAGE 9: CONSTRAINT BUILD [PARALLEL]
│   │   ├─────────────────────────────────────────────────────────────────────
│   │   ├··▶ job_dispatch(constraint_job, ..., &constraint_done)
│   │   └──▶ job_wait_counter(&constraint_done, 0) ◆
│   │
│   │   ┌─────────────────────────────────────────────────────────────────────
│   │   │ STAGE 10: ISLAND BUILD [SYNC - single job]
│   │   ├─────────────────────────────────────────────────────────────────────
│   │   └──▶ phys_stage_island_build(...)  // Union-find, ~50 µs
│   │
│   │   ┌─────────────────────────────────────────────────────────────────────
│   │   │ STAGE 11: TGS SOLVE [PARALLEL - per island]
│   │   ├─────────────────────────────────────────────────────────────────────
│   │   ├··▶ job_dispatch(solve_job[0..M-1], ..., &solve_done)
│   │   │    │
│   │   │    │   M = island_count (1 job per island, or batched small islands)
│   │   │    │
│   │   │    │   void solve_job(void *arg)
│   │   │    │   {
│   │   │    │       // TGS loop for this island
│   │   │    │       // No synchronization needed (islands independent)
│   │   │    │   }
│   │   │    │
│   │   └──▶ job_wait_counter(&solve_done, 0) ◆
│   │
│   │   ┌─────────────────────────────────────────────────────────────────────
│   │   │ STAGE 12: INTEGRATE [PARALLEL]
│   │   ├─────────────────────────────────────────────────────────────────────
│   │   ├··▶ job_dispatch(integrate_job, ..., &integrate_done)
│   │   │    // 512 bodies per job
│   │   └──▶ job_wait_counter(&integrate_done, 0) ◆
│   │
│   │   ┌─────────────────────────────────────────────────────────────────────
│   │   │ STAGE 13: CACHE COMMIT [SYNC]
│   │   ├─────────────────────────────────────────────────────────────────────
│   │   └──▶ phys_stage_cache_commit(...)  // ~20 µs
│   │
│   └─── end substep loop ────────────────────────────────────────────────────
│
├──▶ phys_body_pool_swap_buffers(...)                        [SYNC]
├──▶ world->tick_count++
└── RETURN
```

---

## Network Snapshot Integration

```
═══════════════════════════════════════════════════════════════════════════════
NETWORK SNAPSHOT ENCODE/DECODE (called after tick on server, before on client)
═══════════════════════════════════════════════════════════════════════════════

SERVER SIDE (after physics tick):
─────────────────────────────────

phys_world_tick(world)
        │
        ▼
size_t phys_snapshot_encode(
    const phys_world_t *world,    [IN]  post-tick state
    uint8_t *buffer,              [OUT] encoded bytes
    size_t max_size               [IN]  buffer capacity
) -> bytes_written

        │   IMPLEMENTATION:
        │   ├── Write tick number (uint64_t)
        │   ├── Write body count (uint32_t)
        │   └── For each body:
        │       ├── Quantize position to int16[3] (millimeter precision)
        │       ├── Quantize orientation to int16[3] (smallest-3 quaternion)
        │       ├── Quantize velocities to int16[6]
        │       └── Write flags (uint8_t)
        │
        ▼
size_t phys_snapshot_encode_delta(
    const phys_snapshot_t *prev,  [IN]  previous snapshot
    const phys_snapshot_t *curr,  [IN]  current snapshot
    uint8_t *buffer,              [OUT] delta bytes
    size_t max_size               [IN]  buffer capacity
) -> bytes_written

        │   IMPLEMENTATION:
        │   ├── Write tick delta
        │   ├── Write changed body count
        │   └── For each changed body:
        │       ├── Write body index
        │       └── Write quantized delta values
        │
        ▼
reliable_channel_send(channel, buffer, bytes_written)


CLIENT SIDE (before physics tick):
──────────────────────────────────

reliable_channel_recv(channel, buffer, &size)
        │
        ▼
int phys_snapshot_decode(
    phys_world_t *world,          [OUT] receives decoded state
    const uint8_t *buffer,        [IN]  received bytes
    size_t size                   [IN]  byte count
) -> status

        │   IMPLEMENTATION:
        │   ├── Read tick number
        │   ├── Read body count
        │   └── For each body:
        │       ├── Dequantize position from int16[3]
        │       ├── Dequantize orientation from int16[3]
        │       ├── Dequantize velocities from int16[6]
        │       └── Read flags
        │
        ▼
int phys_snapshot_decode_delta(
    const phys_snapshot_t *prev,  [IN]  previous snapshot (client has)
    phys_snapshot_t *curr,        [OUT] reconstructed current
    const uint8_t *buffer,        [IN]  delta bytes
    size_t size                   [IN]  byte count
) -> status
        │
        ▼
prediction_reconcile(local_state, server_snapshot)
        │
        ▼
phys_world_tick(world)  // Client prediction tick
```

---

## Summary: Stage-by-Stage Dependency Chain

```
═══════════════════════════════════════════════════════════════════════════════
COMPLETE DEPENDENCY CHAIN (data flows left to right)
═══════════════════════════════════════════════════════════════════════════════

TICK PROLOGUE
      │
      ▼
┌───────────┐   step_plan    ┌──────────────┐   tier_lists   ┌─────────────┐
│ StepPlan  │ ─────────────▶ │TierClassify  │ ─────────────▶ │SpatialUpdate│
│ [SYNC]    │                │ [PARALLEL]   │                │ [PARALLEL]  │
└───────────┘                └──────────────┘                └──────┬──────┘
                                                                    │
                                                       aabbs, spatial_grid
                                                                    │
                                                                    ▼
╔═══════════════════════════════════════════════════════════════════════════════╗
║                              SUBSTEP LOOP                                     ║
╠═══════════════════════════════════════════════════════════════════════════════╣
║                                                                               ║
║  tier_lists   ┌───────────┐   tier_lists'  ┌───────────┐   aabbs'            ║
║  ───────────▶ │HaloClosure│ ─────────────▶ │AABBUpdate │ ────────▶           ║
║               │ [PARALLEL]│                │ [PARALLEL]│                     ║
║               └───────────┘                └───────────┘                     ║
║                                                   │                          ║
║                                                   ▼                          ║
║  aabbs, grid  ┌───────────┐   pairs        ┌───────────┐   candidates        ║
║  ───────────▶ │Broadphase │ ─────────────▶ │Narrowphase│ ────────────▶       ║
║               │ [PARALLEL]│                │ [PARALLEL]│                     ║
║               └───────────┘                └───────────┘                     ║
║                                                   │                          ║
║                                                   ▼                          ║
║  candidates   ┌───────────┐   manifolds    ┌───────────┐   hints             ║
║  + cache ───▶ │ManifoldBld│ ─────────────▶ │Stabilize  │ ────────────▶       ║
║               │ [PARALLEL]│                │ [PARALLEL]│                     ║
║               └───────────┘                └───────────┘                     ║
║                                                   │                          ║
║                                                   ▼                          ║
║  manifolds    ┌───────────┐  constraints   ┌───────────┐   islands           ║
║  + hints ───▶ │ConstBuild │ ─────────────▶ │IslandBuild│ ────────────▶       ║
║               │ [PARALLEL]│                │ [SYNC]    │                     ║
║               └───────────┘                └───────────┘                     ║
║                                                   │                          ║
║                                                   ▼                          ║
║  islands      ┌───────────┐   velocities   ┌───────────┐   bodies_out        ║
║  + const ───▶ │ TGS Solve │ ─────────────▶ │ Integrate │ ────────────▶       ║
║               │ [PARALLEL]│                │ [PARALLEL]│                     ║
║               └───────────┘                └───────────┘                     ║
║                                                   │                          ║
║                                                   ▼                          ║
║  manifolds    ┌───────────┐   cache'                                         ║
║  + λ values ▶ │CacheCommit│ ─────────────▶ (persists to next frame)          ║
║               │ [SYNC]    │   + events                                       ║
║               └───────────┘                                                  ║
║                                                                               ║
╚═══════════════════════════════════════════════════════════════════════════════╝
      │
      ▼
┌───────────┐
│BufferSwap │   swap(bodies_curr, bodies_next)
│ [SYNC]    │
└───────────┘
      │
      ▼
TICK EPILOGUE
```

---

## File Appendix: Implementation Locations

| Stage | Header | Source | Tests |
|-------|--------|--------|-------|
| Step Plan | `include/ferrum/physics/step_plan.h` | `src/physics/stages/step_plan.c` | `tests/physics/step_plan_tests.c` |
| Tier Classify | `include/ferrum/physics/tier_classify.h` | `src/physics/stages/tier_classify.c` | `tests/physics/tier_classify_tests.c` |
| Spatial Update | `include/ferrum/physics/spatial_update.h` | `src/physics/stages/spatial_update.c` | `tests/physics/spatial_update_tests.c` |
| Halo Closure | `include/ferrum/physics/halo_closure.h` | `src/physics/stages/halo_closure.c` | `tests/physics/halo_closure_tests.c` |
| AABB Update | `include/ferrum/physics/aabb_update.h` | `src/physics/stages/aabb_update.c` | `tests/physics/aabb_update_tests.c` |
| Broadphase | `include/ferrum/physics/broadphase.h` | `src/physics/stages/broadphase.c` | `tests/physics/broadphase_tests.c` |
| Narrowphase | `include/ferrum/physics/narrowphase.h` | `src/physics/stages/narrowphase.c` | `tests/physics/narrowphase_tests.c` |
| Manifold Build | `include/ferrum/physics/manifold_build.h` | `src/physics/stages/manifold_build.c` | `tests/physics/manifold_build_tests.c` |
| Stabilization | `include/ferrum/physics/stabilization.h` | `src/physics/stages/stabilization.c` | `tests/physics/stabilization_tests.c` |
| Constraint Build | `include/ferrum/physics/constraint_build.h` | `src/physics/stages/constraint_build.c` | `tests/physics/constraint_build_tests.c` |
| Island Build | `include/ferrum/physics/island_build.h` | `src/physics/stages/island_build.c` | `tests/physics/island_build_tests.c` |
| TGS Solve | `include/ferrum/physics/tgs_solve.h` | `src/physics/solver/tgs_solve.c` | `tests/physics/tgs_solve_tests.c` |
| XPBD Solve | `include/ferrum/physics/xpbd_solve.h` | `src/physics/solver/xpbd_solve.c` | `tests/physics/xpbd_solve_tests.c` |
| Solver Transition | — (internal) | `src/physics/solver/solver_transition.c` | `tests/physics/solver_transition_tests.c` |
| Integrate | `include/ferrum/physics/integrate.h` | `src/physics/stages/integrate.c` | `tests/physics/integrate_tests.c` |
| Cache Commit | `include/ferrum/physics/cache_commit.h` | `src/physics/stages/cache_commit.c` | `tests/physics/cache_commit_tests.c` |
| Snapshot | `include/ferrum/physics/snapshot.h` | `src/physics/net/snapshot_encode.c`, `snapshot_decode.c` | `tests/physics/snapshot_tests.c` |
| World | `include/ferrum/physics/world.h` | `src/physics/world/world.c`, `tick.c` | `tests/physics/world_tests.c` |

---

## Cross-Reference: physics_plan.md

This call graph implements all stages defined in `ref/physics_plan.md`:

| Plan Section | This Document Section |
|--------------|----------------------|
| Phase 0: Foundation Data Structures | Data Structures Overview |
| Phase 1, Step 1.1 (Stage 0) | STAGE 0: STEP PLAN |
| Phase 1, Step 1.2 (Stage 1) | STAGE 1: BASE TIER CLASSIFICATION |
| Phase 1, Step 1.3 (Stage 2) | STAGE 2: SPATIAL INDEX UPDATE |
| Phase 1, Step 1.4 (Stage 3) | STAGE 3: HALO CLOSURE |
| Phase 1, Step 1.5 (Stage 4) | STAGE 4: AABB UPDATE |
| Phase 1, Step 1.6 (Stage 5) | STAGE 5: BROADPHASE |
| Phase 1, Step 1.7 (Stage 6) | STAGE 6: NARROWPHASE |
| Phase 1, Step 1.8 (Stage 7) | STAGE 7: MANIFOLD BUILD |
| Phase 1, Step 1.9 (Stage 8) | STAGE 8: STABILIZATION |
| Phase 1, Step 1.10 (Stage 9) | STAGE 9: CONSTRAINT BUILD |
| Phase 1, Step 1.11 (Stage 10) | STAGE 10: ISLAND BUILD (T0/T1 ONLY) |
| Phase 1, Step 1.12 (Stage 11a/11b) | STAGE 11a: TGS SOLVE / STAGE 11b: XPBD SOLVE |
| Phase 1, Step 1.13 (Stage 12) | STAGE 12: INTEGRATE |
| Phase 1, Step 1.14 (Stage 13) | STAGE 13: CACHE COMMIT |
| Phase 1, Step 1.15 (Stage 14) | STAGE 14: BUFFER SWAP |
| Phase 1, Step 1.16 | Network Snapshot Integration |
| Phase 3: Parallel Jobs | Parallel Job Execution Model |
