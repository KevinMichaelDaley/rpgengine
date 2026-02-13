OVERVIEW: Tiered TGS physics architecture (XPBD planned)
(for a fiber-based ECS engine with sparse interactions)

CURRENT IMPLEMENTATION STATUS (source-of-truth: src/physics/world/tick*.c)
- Tick orchestrators run TGS with split impulse; XPBD modules exist but are not wired.
- Position projection / velocity sync are fused into TGS via split impulse (pseudo_velocities[]).
- Halo closure + broadphase run once per tick (outside the substep loop).
- Joint system is implemented: distance, ball, and hinge joints.
- TGS solver has adaptive per-island iteration scaling and sub-substeps.
- Nonlinear joint position projection runs after TGS iterations.

==============================================================
1) WHAT THE ARCHITECTURE IS OPTIMIZING FOR
==============================================================

- Server-authoritative physics at 30 Hz.
- Deterministic-leaning, island-based simulation.
- Thousands of “relevant” objects, but a sparse interaction graph.
- Strong gameplay feel:
  - stacks don’t creep,
  - resting objects feel stable,
  - player-manipulated objects feel crisp and intentional.
- A Naughty-Dog-style engine:
  - fixed worker threads,
  - fibers as the unit of execution,
  - no heavyweight external job scheduler.

The key principle:
  KEEP ONE PHYSICS PIPELINE, BUT RUN IT AT DIFFERENT FIDELITIES
  USING TIERS, BUDGETS, AND STABILIZATION HINTS.

==============================================================
2) CORE DESIGN CHOICES
==============================================================

A) ONE WORLD STATE, MULTIPLE VIEWS
---------------------------------
- There is exactly one SoA physics state:
    PoseIn / VelIn → PoseOut / VelOut
    AABB, collider refs, mass/inertia, rest/sleep state
- No per-tier copies of entity state.
- Tiers are just filtered, packed lists of body indices:
    TierList[T] = { indices into shared SoA }

This keeps consistency and avoids “multiple simulations”.

--------------------------------------------------------------

B) STABILIZATION IS FIRST-CLASS (FROM DAY ONE)
----------------------------------------------
- Stabilization is NOT a post-processing hack.
- It is a dedicated pass between:
    Manifold build → Constraint build

Stabilization produces hints, not forces:
- friction boost near zero slip
- restitution suppression when resting
- optional tiny tangential soft clamp
- tier-dependent sleep thresholds

Friction uses a Coulomb cone model: friction constraint lambda bounds are
clamped to ±µ · λ_normal · friction_scale, where µ is the combined friction
coefficient and friction_scale incorporates tier-dependent boosting.

This directly shapes:
- manifold caching quality
- solver convergence
- “static equilibrium” feel

--------------------------------------------------------------

C) ISLANDS ARE THE BASE UNIT OF SOLVE
------------------------------------
- Contacts/constraints form a sparse graph.
- Union-find builds islands.
- Each island is solved independently.
- Solvers write only to output buffers.

Result:
- zero write contention during solve
- natural parallelism
- perfect fit for sparse interaction graphs

--------------------------------------------------------------

D) TIERS ARE ABOUT INTERACTION AUTHORITY, NOT JUST DISTANCE
-----------------------------------------------------------

Typical tier set:

T0 – Direct Manipulation
  Grabbed objects, piloted vehicles, tight player control.
  Highest fidelity, strongest stabilization.

T1 – Near Interactive
  Same room, within a few seconds’ walk, or visible through a window/doorway.
  Includes large objects seen through openings. Occluded nearby objects demote to T3.
  Stable resting, high contact quality.

T2 – Visible / Potentially Hazardous
  Visible behind barriers, stacks that could fall, traps.
  Reduced fidelity, still correct.

T3 – World-Shaping
  Far but consequential: boulders, large movers, collapsing structures.
  Fidelity based on predicted impact, not distance.

T4 – Background Dynamic
  Huge count, low consequence.
  Amortized stepping, aggressive sleep.

T5 – Sleeping / Dormant
  Not simulated, wake via events.

Tiers change PARAMETERS, SCHEDULING, and SOLVER MODE, not the pipeline itself.

--------------------------------------------------------------

E) PROMOTION IS A CLASSIFICATION PASS, NOT A SOLVER SIDE EFFECT
--------------------------------------------------------------

Promotion is explicit and deterministic:

1) Base classification (once per tick):
   - distance, visibility, hazard heuristics
   - manipulation flags
   - occlusion override: renderer provides visibility_set bitfield;
     nearby but occluded bodies demote to T3+
     (they only need to sound right and land plausibly)
   - hysteresis to prevent flapping (includes occlusion hysteresis)

2) Halo closure (currently once per tick in tick.c; Tier0/T1-focused):
   - swept AABB + margin
   - spatial query for neighbors
   - promote neighbors conservatively

After this:
- TierLists are final for the substep.
- Solvers never “discover” missing bodies mid-solve.

--------------------------------------------------------------

F) SOLVER: TGS (CURRENT) + XPBD (PLANNED)
--------------------------------------------------------------

Current `tick.c` / `tick_parallel.c` behavior:

  All tiers  →  TGS (Temporal Gauss-Seidel), island-based
  (XPBD modules exist but are not currently invoked by the tick orchestrators)

Why two solvers?

TGS gives the best convergence and "feel" for near-field objects:
stable resting contacts, crisp manipulation, no visible drift.
But it is sequential within islands (Gauss-Seidel ordering) and
its cost scales with the largest island—which means a far-field
explosion that merges 200 bodies into one island can blow the
budget even though the player doesn't notice the difference.

Jacobi-style XPBD solves this by being unconditionally stable
and embarrassingly parallel: every body can be processed
independently in the same iteration pass. The tradeoff is slower
convergence and mushier stacking, but for T2–T4 objects this is
invisible to the player. More importantly, it parallelizes over
bodies, not islands, so an explosion of 200 far-field objects
costs the same as 200 independent bodies—no giant-island
bottleneck at all.

F.1) XPBD RECAP
- Position-based: constraints produce positional corrections Δx
  and accumulated Lagrange multipliers λ (not impulses).
- Compliance α controls stiffness:
    Δλ = (-C - α̃·λ) / (∇C^T M^-1 ∇C + α̃)
    Δx = M^-1 ∇C^T Δλ
  where α̃ = α / dt².
- Jacobi variant: all corrections computed from start-of-iteration
  state, then blended (ω ≈ 0.5–0.8 for damping). This removes
  ordering dependency at the cost of slower convergence, but enables
  full per-body parallelism.

F.2) TRANSITION LOGIC (TGS ↔ XPBD)

When a body transitions between solver domains (tier demotion T1→T2
or promotion T2→T1), its cached solver state must be converted so
warm-starting remains effective.

TGS → XPBD (body demoted from T1 to T2):
  The TGS solver stores accumulated impulses λ_impulse per constraint
  row. To seed XPBD warm-start multipliers:
    1. Recover the positional correction that impulse would have produced:
         Δx ≈ M^-1 · J^T · λ_impulse · dt
    2. Compute the equivalent XPBD multiplier:
         λ_xpbd = λ_impulse · dt
       XPBD multipliers have units of [impulse × time] (they accumulate
       position-scaled corrections). The compliance term α̃ is then
       applied naturally during the first XPBD iteration, so no explicit
       compliance conversion is needed here.
    3. Store λ_xpbd in the manifold cache alongside the contact ID.

XPBD → TGS (body promoted from T2 to T1):
  The XPBD solver stores Lagrange multipliers λ_xpbd.
  To seed TGS warm-start impulses:
    1. Compute the impulse that would produce the same velocity change:
         λ_impulse = λ_xpbd / dt
    2. Clamp to TGS lambda bounds [λ_min, λ_max] to avoid injecting
       energy from the softer XPBD solution.
    3. Apply λ_impulse as initial warm-start in the first TGS iteration.

  The clamping in step 2 is essential: XPBD with compliance > 0 may
  have accumulated multipliers that correspond to softer-than-rigid
  behavior. Injecting them as rigid impulses would over-correct.
  Clamping ensures the TGS solver converges from a safe starting point.

Cache storage:
  The manifold cache stores both representations:
    phys_manifold_cache_entry_t {
        ...
        float lambda_impulse[3];   // TGS warm-start (normal + 2 friction)
        float lambda_xpbd[3];     // XPBD warm-start (same layout)
        uint8_t last_solver_mode;  // 0 = TGS, 1 = XPBD
    }
  On transition, the conversion runs once (in the constraint build
  stage) and overwrites the destination field. The last_solver_mode
  flag tells constraint build which conversion to apply.

F.3) POSITION PROJECTION (REPLACES BAUMGARTE FOR T0/T1)

  TGS solves velocity-level constraints, but velocity-level Baumgarte bias
  alone produces soft, drifty position correction that causes visible
  penetration under stacking loads.

  Position projection is a post-solve pass that directly corrects
  penetration at the position level for T0/T1 islands. It runs per-island
  after the TGS solve and before integration:

  1. For each island, extract the penetration depth Φ(q) from each normal
     constraint row (row 0), applying slop threshold.
  2. Assemble the per-island Schur complement:
       A = J · M⁻¹ · Jᵀ    (dense, nc × nc where nc = island constraint count)
  3. Solve for correction multipliers:
       A · λ = −Φ(q)         (dense LDL^T factorization)
  4. Clamp λ ≥ 0 (contacts only push apart).
  5. Compute position corrections:
       Δq = M⁻¹ · Jᵀ · λ
  6. Velocity synchronization: replace only the constraint-normal component
     of each body's velocity with the correction velocity (Δq/dt).
     Tangential velocity from TGS friction solving is preserved.

  The velocity sync step (step 6) is critical: naive v = Δq/dt would
  destroy the tangential friction velocity that TGS computed. Instead,
  for each body, we scan its contact normals, strip the old normal
  component of velocity, and replace it with the correction's normal
  component. See ref/sparse_stabilization.tex Section 5b.

  Implementation:
    - position_projection.h / position_projection.c: main entry point
    - ldlt_solve.c: dense LDL^T factorization (O(nc³), fast for small islands)
    - velocity_sync.h / velocity_sync.c: normal-component replacement

  Note: Baumgarte bias is still computed in constraint_build.c and used
  by TGS for velocity-level warm convergence. Position projection
  supplements it with direct geometric correction. The `penetration`
  field on phys_constraint_t stores the raw depth for this purpose.

F.4) WHY THIS SCALES

  T0/T1 (TGS):
    - Typically < 50 near-field bodies → small islands
    - 24 iterations, sequential per island, but islands are small
    - Best convergence for player-visible interactions

  T2–T4 (Jacobi XPBD):
    - Hundreds or thousands of far-field bodies
    - 2–8 iterations: T2:8, T3:4, T4:2 (reduced at distance for throughput)
    - Each body processed independently → one job per 128 bodies
    - A 200-body explosion is just 2 jobs, not one giant island
    - Unconditionally stable: overshooting never diverges, it just
      looks mushy (acceptable in the far field)
    - Sphere simplification at T2+: bodies with bounding-sphere ratio
      (circumradius/inradius) < 1.3 use sphere collider + sphere-sphere
      narrowphase + simplified constraint (1 row, scalar effective_mass)
    - T4 bodies prefer sphere collider unconditionally for small objects

  The transition boundary (T1↔T2) is where the conversion runs.
  Because tier classification has hysteresis (bodies keep their tier
  for K frames), transitions are rare—typically single-digit bodies
  per tick during steady state.

F.5) CONSTRAINT SPECIALIZATION

  The generic constraint processes 3 Jacobian rows per contact point
  (1 normal + 2 friction tangents). Two common cases have fast-paths:

  PLANAR CONTACTS (coplanar manifold points, e.g. box-on-box):
    - All contact points share the same face normal
    - Normal Jacobian J_v is identical; only J_w differs per point
    - Build 1 shared normal solve + 1 shared friction solve +
      cheap per-point angular residuals
    - 4-point box-on-box: 12 rows → ~4 effective rows (3× cheaper)
    - SLEEP PROPAGATION: if all planar contact points are below a
      velocity threshold, the entire contact-graph subtree below
      can remain sleeping. A 5-box stack disturbed at top: only the
      top box needs full solve (15× reduction for deep stacks)

  SPHERE-SPHERE CONTACTS:
    - 1 manifold point per intersection (guaranteed)
    - Trivial Jacobian: J_v = ±n, J_w = ±(radius × n), no cross products
    - Effective mass is scalar (diagonal inertia for sphere)
    - 1–2 rows instead of 3, each ~2× cheaper = ~6× total speedup

  Constraint build (Stage 9) detects these cases:
    - Planar: all manifold normals within ε of each other
    - Sphere: both colliders are spheres (or sphere-simplified at T2+)
  The specialized constraint uses the same phys_constraint_t struct
  but with fewer active rows and precomputed shared Jacobians.

--------------------------------------------------------------

G) BROADPHASE CHOICE FOR WIDE-OPEN, SPARSE WORLDS
-------------------------------------------------

- Static world: static BVH (built once).
- Dynamic bodies: hashed uniform grid.
  - grid payload stored as contiguous arrays (CSR-style)
  - cache-coherent neighbor scans
- Existing global z-order list is reused for:
  - stable iteration order
  - determinism
  - other engine systems

Grid is used for:
- broadphase DD
- halo closure neighbor queries

==============================================================
3) DATAFLOW (DFD)
==============================================================

PER TICK (30 Hz)
----------------
1) Build StepPlan:
   - which tiers step this tick
   - substeps, iterations, budgets
2) Base tier classification.
3) Update / maintain spatial index for relevant bodies.
4) Run N substeps (usually 2, sometimes 3 for Tier0).

PER SUBSTEP
-----------
0) Halo closure / promotion.
1) Active set + AABB update.
2) Broadphase:
   - dynamic vs static BVH
   - dynamic vs dynamic grid queries
3) Narrowphase → ContactCandidates[T].
4) Manifold build + cache merge (≤ K points).
5) Stabilization hint generation.
6) Constraint build (contacts only in MVP).
7) Island build (union-find, T0/T1 only).
8) Solve:
   - T0/T1: TGS solve (batched islands)
   - T2–T4: Jacobi XPBD solve (per-body parallel)
9) Integrate + sleep + cache commit.
10) Emit impact events (future fracture / gameplay).

==============================================================
4) SCHEDULING MODEL (FIBER-BASED)
==============================================================

- One fiber runtime.
- Work is chunked into batched jobs.
- Each tier has:
    - a per-stage cursor
    - an in-flight cap
    - a time budget (microseconds)

WORK PUMPING MODEL:
- Do NOT enqueue all work for a tier.
- Spawn jobs until:
    inFlight < cap AND budget > 0
- When a job completes:
    - decrement inFlight
    - subtract time from budget
    - enqueue next chunk if allowed

This ensures:
- Tier0 completes quickly.
- Tier3 isn’t starved.
- Tier4 cannot flood the system.

Optional:
- multiple runnable queues (hi/mid/low) for fibers.

==============================================================
5) FEEL & STABILITY TUNING (BUILT-IN)
==============================================================

MINIMUM FEEL GUARANTEES:
- persistent manifolds
- warmstarting
- deterministic ordering
- stabilization hints
- tier-aware sleep

TYPICAL STARTING PARAMETERS:

Tier 0 – Direct Manipulation
  solver: TGS (island-based)
  substeps: 3 (while active), else 2
  iterations: 24
  manifold points: 4
  friction boost: ~3x
  restitution: suppressed when stabilized

Tier 1 – Near Interactive (same room / few seconds’ walk / visible through window)
  solver: TGS (island-based)
  substeps: 2
  iterations: 20
  manifold points: 4
  moderate stabilization
  occlusion override: nearby but occluded bodies demote to T3

Tier 2 – Visible / Hazardous
  solver: Jacobi XPBD (per-body parallel)
  substeps: 1
  iterations: 8
  manifold points: 2
  light stabilization
  compliance: 1e-6 (near-rigid)

Tier 3 – World-Shaping
  solver: Jacobi XPBD (per-body parallel)
  substeps: 1–2 based on speed/impact
  iterations: 4
  collider: sphere simplification if bounding-sphere ratio < 1.3
  stabilization off while moving
  compliance: 1e-5 (slightly soft)

Tier 4 – Background
  solver: Jacobi XPBD (per-body parallel)
  amortized cadence (e.g. 10 Hz)
  iterations: 2
  collider: sphere preferred (all small objects)
  manifold points: 1
  aggressive sleep
  compliance: 1e-4 (soft, energy-absorbing)

==============================================================
6) PERFORMANCE TARGETS
==============================================================

NEAR-PLAYER INDOOR (≈25 interacting bodies):
- Total physics per tick: ~0.8–2.0 ms (2 substeps)

PER SUBSTEP ROUGH TARGETS:
- tier + halo:        20–120 µs
- AABB update:        20–80 µs
- broadphase:         40–200 µs
- narrowphase:        80–350 µs
- manifold + cache:   25–120 µs
- stabilization:      15–120 µs
- island build:       20–150 µs
- solve (TGS+XPBD):   150–600 µs
- integrate/sleep:    20–120 µs

THOUSANDS OF BODIES, SPARSE GRAPH:
- broadphase + halo:  0.5–2.0 ms
- narrowphase/manifold: 0.5–3.0 ms
- solve (TGS T0/T1): 0.1–0.5 ms (small island count)
- position projection (T0/T1): 0.05–0.2 ms (dense LDL^T per island)
- solve (XPBD T2–T4): 0.3–2.0 ms (parallel over bodies)
- background amortized: ≤0.5–1.0 ms

--------------------------------------------------------------
6.1) DEFAULT CONFIG VALUES (phys_world_config_default)
--------------------------------------------------------------

| Parameter                | Value  | Notes                                     |
|--------------------------|--------|-------------------------------------------|
| fixed_dt                 | 1/60   | 60 Hz tick rate                           |
| max_dt_override          | 3.0    | Variable-dt cap (multiplier of fixed_dt)  |
| gravity                  | -9.81  | Y-down                                    |
| default_substeps         | 1      | Per-tier substep counts override this     |
| default_solver_iterations| 8      | TGS constraint iterations per substep     |
| baumgarte                | 0.02   | Position correction bias strength         |
| slop                     | 0.005  | 5 mm penetration tolerance before bias    |
| sleep_threshold_linear   | 0.10   | m/s — body sleeps below this              |
| sleep_threshold_angular  | 0.10   | rad/s — body sleeps below this            |
| sleep_delay_frames       | 60     | Frames below threshold before sleep (1s)  |
| warmstart_decay          | 0.95   | Impulse carry-over decay per frame        |
| velocity_damping         | 0.99   | Fraction retained per second              |

Adaptive solver tuning (compile-time constants in tgs_solve.c):
| Parameter                | Value  | Notes                                     |
|--------------------------|--------|-------------------------------------------|
| SOR_OMEGA                | 1.1    | Successive over-relaxation factor         |
| ADAPTIVE_SPEED_LOW       | 5 m/s  | Below this: use base iterations           |
| ADAPTIVE_SPEED_HIGH      | 200 m/s| At this: max iterations (capped by island)|
| ADAPTIVE_ITER_MULT       | 5      | Max multiplier on base iteration count    |
| SUBSUB_SPEED_LOW         | 15 m/s | Below this: 1 sub-substep                 |
| SUBSUB_SPEED_HIGH        | 150 m/s| At this: max sub-substeps                 |
| SUBSUB_MAX               | 8      | Maximum solver sub-substeps per island    |
| NL_PROJ_PASSES           | 4      | Nonlinear projection passes after TGS     |
| NL_PROJ_FRACTION         | 0.8    | Error fraction corrected per pass         |
| NL_PROJ_MIN_ERROR        | 0.01 m | Min anchor error to trigger projection    |

Large-island adaptive iteration caps (constraint count based):
| Constraint count         | Mult   | Notes                                     |
|--------------------------|--------|-------------------------------------------|
| ≤ 256                    | 5×     | Full adaptive multiplier                  |
| 257–512                  | 3×     | Moderate cap to control worst-case        |
| > 512                    | 2×     | Minimal cap for very large islands        |

SOR tuning notes:
- SOR omega=1.1 provides mild over-relaxation that accelerates convergence
  without overshooting Coulomb cone friction bounds.
- Omega ≥ 1.3 causes friction rows to overshoot, resulting in box sliding.
- Warmstarting lambda writeback provides a better initial guess, allowing
  fewer iterations (base 8 vs 10) while maintaining stability.

Variable-dt fallback (phys_tick_runner.c):
- The tick runner tracks a rolling 16-tick history of overrun vs on-time ticks.
- When 12+ of the last 16 ticks exceeded the target period, the runner switches
  to variable-dt mode: dt_override is set to actual wall-clock elapsed time.
- dt_override is clamped to max_dt_override × fixed_dt (default 3×, i.e. 50 ms).
- When performance recovers (< 12 overruns), dt_override resets to 0 and the
  simulation returns to fixed timestep.
- This prevents time accumulation debt (the "spiral of death") under sustained
  load by letting the simulation run slower but without skipping simulation time.

Joint constraint tuning (set per-joint at creation):
| Parameter                | Value  | Notes                                     |
|--------------------------|--------|-------------------------------------------|
| joint.damping            | 0.1–0.5| Row-level viscous damping (must be < 1)   |
| Baumgarte leak lo        | 5 m/s  | Speed below which leak = 0                |
| Baumgarte leak hi        | 60 m/s | Speed at which leak = 0.6                 |
| Predictive blend lo      | 5 m/s  | Speed below which blend = 0              |
| Predictive blend hi      | 80 m/s | Speed at which blend = 0.5               |

Tuning notes:
- baumgarte too high (>0.05) causes ground-contact oscillation/jitter.
- slop too low (<0.002) blocks sleep via persistent micro-penetration.
- sleep thresholds too tight (<0.05) prevent resting objects from sleeping
  due to solver noise at contact points.
- Joint damping > 1.0 causes PGS divergence/oscillation.
- Nonlinear (quadratic) damping was tried and failed — use linear only.
- Adaptive iterations use sqrt ramp for fast convergence at moderate speeds.

==============================================================
7) WHY THIS ARCHITECTURE SCALES WITHOUT REWRITES
==============================================================

- Pipeline shape is fixed:
    Contacts → Manifolds → Constraints → Islands(T0/T1) → Solve(TGS|XPBD)
    → PositionProjection(T0/T1) → Integrate
- Stabilization is parameterized, not hard-coded.
- Tiers affect scheduling, quality, and solver mode, not correctness.
- New features (joints, articulation, fracture, integrity):
    - plug in as new constraint/event consumers
    - do not change the core DFD.

RESULT:
You can build this incrementally, test it early for “feel”,
and scale it to large worlds without replacing the solver
or rewriting the architecture.


==============================================================
8) FERRUM ENGINE INTEGRATION: DETAILED DATAFLOW ARCHITECTURE
==============================================================

This section maps the physics pipeline to Ferrum's concrete
subsystems: homogeneous pools, job system, pure functional
stages, and memory arenas.

--------------------------------------------------------------
8.1) PHYSICS DATA POOLS (HOMOGENEOUS ENTITY STORAGE)
--------------------------------------------------------------

Physics uses dedicated homogeneous pools for maximum throughput.
All hot-path data is stored contiguously, enabling SIMD and
cache-optimal iteration.

POOL: phys_body_pool (rigid body state)
---------------------------------------
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
    uint8_t sleep_counter;        // 1 byte (consecutive low-velocity frames)
    uint8_t pad[6];               // alignment
} phys_body_t;                    // 80 bytes

pool_t phys_body_pool;       // capacity: 16k bodies
```

POOL: phys_aabb_pool (broadphase bounds)
----------------------------------------
```c
typedef struct phys_aabb_t {
    vec3_t min;
    vec3_t max;
} phys_aabb_t;               // 24 bytes

pool_t phys_aabb_pool;       // 1:1 with body pool
```

POOL: phys_collider_pool (shape references)
-------------------------------------------
```c
typedef struct phys_collider_t {
    uint32_t shape_type;     // sphere, box, capsule, convex, mesh
    uint32_t shape_index;    // into shape-specific pool
    vec3_t local_offset;
    quat_t local_rotation;
} phys_collider_t;           // 36 bytes

pool_t phys_collider_pool;   // 1:1 with body pool
```

ARENA: phys_frame_arena (per-tick transients)
---------------------------------------------
Used for all temporary allocations that don't persist across ticks:
- collision pairs
- contact candidates
- manifold scratch
- constraint Jacobians
- island membership lists

Reset at end of physics tick. Size: ~4 MB typical.

ARENA: phys_substep_arena (per-substep transients)
--------------------------------------------------
Nested arena within frame arena for substep-local allocations.
Reset after each substep. Avoids fragmentation within substeps.

--------------------------------------------------------------
8.2) DOUBLE-BUFFERED STATE (PURE FUNCTIONAL I/O)
--------------------------------------------------------------

Following Ferrum's core principle: stages read from input buffers
and write to distinct output buffers.

POSE/VELOCITY BUFFERS (ping-pong):
```c
// Allocated from level arena (persists across frames)
phys_body_t *bodies_curr;    // read by solver/integrate
phys_body_t *bodies_next;    // written by integrate

// Swap at end of tick
void phys_swap_buffers(void) {
    phys_body_t *tmp = bodies_curr;
    bodies_curr = bodies_next;
    bodies_next = tmp;
}
```

For solver iterations within a substep:
- TGS (T0/T1): in-place velocity updates (Gauss-Seidel pattern),
  position integration is staged.
- XPBD (T2–T4): positional corrections accumulated per body
  (Jacobi pattern), velocities derived from position change / dt.

--------------------------------------------------------------
8.3) TIER LISTS (FILTERED VIEWS)
--------------------------------------------------------------

Tier lists are packed index arrays pointing into the shared pools.
Built once per tick (base classification) and refined per substep
(halo closure).

```c
typedef struct phys_tier_list_t {
    uint32_t *indices;       // arena-allocated
    uint32_t count;
    uint32_t capacity;
} phys_tier_list_t;

phys_tier_list_t tier_lists[6];  // T0–T5
```

--------------------------------------------------------------
8.4) COMPLETE DATAFLOW DIAGRAM
--------------------------------------------------------------

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
│                                                                             │
│ Single job, ~10 µs. Determines simulation fidelity for this tick.           │
└─────────────────────────────────────────────────────────────────────────────┘
                                              │
                                              ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ STAGE 1: BASE TIER CLASSIFICATION                                  [PARALLEL]│
├─────────────────────────────────────────────────────────────────────────────┤
│ read:  bodies_curr (positions), player_positions[], manipulation_flags[]    │
│ write: tier_lists[0..5] (arena-allocated index arrays)                      │
│                                                                             │
│ Jobs: N parallel (1k bodies/job)                                            │
│ Counter: tier_class_done                                                    │
│                                                                             │
│ Hysteresis: bodies keep tier for K frames unless triggered to change.       │
│ Output: packed index arrays per tier.                                       │
└─────────────────────────────────────────────────────────────────────────────┘
                                              │
                                              ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ STAGE 2: SPATIAL INDEX UPDATE                                      [PARALLEL]│
├─────────────────────────────────────────────────────────────────────────────┤
│ read:  bodies_curr (positions), colliders (for AABB computation)            │
│ write: aabbs[], spatial_grid (hash grid cells)                              │
│                                                                             │
│ Jobs: N parallel (512 bodies/job)                                           │
│ Counter: spatial_done                                                       │
│                                                                             │
│ Grid: uniform hash grid with CSR-style cell storage.                        │
│ Static BVH: updated incrementally or skipped if unchanged.                  │
└─────────────────────────────────────────────────────────────────────────────┘
                                              │
                                              ▼
                    ╔═════════════════════════════════════════════════════════╗
                    ║         SUBSTEP LOOP (repeat N times per tick)          ║
                    ║         reset phys_substep_arena each iteration         ║
                    ╚═════════════════════════════════════════════════════════╝
                                              │
                                              ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ STAGE 3: HALO CLOSURE                                              [PARALLEL]│
├─────────────────────────────────────────────────────────────────────────────┤
│ read:  tier_lists[T0], bodies_curr (velocities), spatial_grid              │
│ write: tier_lists[T0..T1] (expanded with promoted neighbors)                │
│                                                                             │
│ For T0 bodies: swept AABB + margin → query grid → promote neighbors to T1   │
│                                                                             │
│ Jobs: 1 per T0 body (small count, high priority)                            │
│ Counter: halo_done                                                          │
└─────────────────────────────────────────────────────────────────────────────┘
                                              │
                                              ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ STAGE 4: AABB UPDATE (ACTIVE SET)                                  [PARALLEL]│
├─────────────────────────────────────────────────────────────────────────────┤
│ read:  tier_lists[T0..T4], bodies_curr, colliders                           │
│ write: aabbs[] (only for active bodies)                                     │
│                                                                             │
│ Jobs: per-tier parallel batches                                             │
│ Counter: aabb_done                                                          │
└─────────────────────────────────────────────────────────────────────────────┘
                                              │
                                              ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ STAGE 5: BROADPHASE                                                [PARALLEL]│
├─────────────────────────────────────────────────────────────────────────────┤
│ read:  aabbs[], spatial_grid, static_bvh                                    │
│ write: collision_pairs[] (arena-allocated pair array)                       │
│                                                                             │
│ Sub-stages:                                                                 │
│   5a) Dynamic vs Static BVH queries                                         │
│   5b) Dynamic vs Dynamic grid queries (self-collision)                      │
│                                                                             │
│ Jobs: grid cells partitioned across jobs                                    │
│ Counter: broad_done                                                         │
│                                                                             │
│ Output: unsorted pair list, may contain duplicates (dedupe in narrowphase). │
└─────────────────────────────────────────────────────────────────────────────┘
                                              │
                                              ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ STAGE 6: NARROWPHASE                                               [PARALLEL]│
├─────────────────────────────────────────────────────────────────────────────┤
│ read:  collision_pairs[], bodies_curr, colliders, shape_pools              │
│ write: contact_candidates[] (arena-allocated)                               │
│                                                                             │
│ Per-pair: GJK/EPA or specialized primitive tests.                           │
│ Sphere simplification: if tier >= T3 and sphere_simplify flag set, override │
│ collider to bounding sphere. Both T2+ bodies qualifying → sphere-sphere.   │
│ Output: contact point, normal, penetration depth.                           │
│                                                                             │
│ Jobs: N parallel (64 pairs/job)                                             │
│ Counter: narrow_done                                                        │
└─────────────────────────────────────────────────────────────────────────────┘
                                              │
                                              ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ STAGE 7: MANIFOLD BUILD + CACHE MERGE                              [PARALLEL]│
├─────────────────────────────────────────────────────────────────────────────┤
│ read:  contact_candidates[], manifold_cache (persistent)                    │
│ write: manifolds[] (arena-allocated), manifold_cache (updated)              │
│                                                                             │
│ Merge new contacts with cached contacts (≤ K points, typically 4).          │
│ Persistent IDs enable warmstarting.                                         │
│                                                                             │
│ Jobs: N parallel (32 pairs/job)                                             │
│ Counter: manifold_done                                                      │
└─────────────────────────────────────────────────────────────────────────────┘
                                              │
                                              ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ STAGE 8: STABILIZATION HINTS                                       [PARALLEL]│
├─────────────────────────────────────────────────────────────────────────────┤
│ read:  manifolds[], bodies_curr (velocities), tier_lists                    │
│ write: stab_hints[] (per-manifold hints: friction boost, restitution mod)   │
│                                                                             │
│ Classify each contact:                                                      │
│   - resting (low relative velocity) → boost friction, suppress bounce       │
│   - separating → normal contact response                                    │
│   - sliding → dynamic friction                                              │
│                                                                             │
│ Jobs: N parallel (64 manifolds/job)                                         │
│ Counter: stab_done                                                          │
└─────────────────────────────────────────────────────────────────────────────┘
                                              │
                                              ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ STAGE 9: CONSTRAINT BUILD                                          [PARALLEL]│
├─────────────────────────────────────────────────────────────────────────────┤
│ read:  manifolds[], stab_hints[], bodies_curr (mass, inertia)               │
│ write: constraints[] (arena-allocated Jacobian rows)                        │
│                                                                             │
│ For each contact point: build contact constraint (normal + 2 friction).     │
│ Future: joint constraints plug in here.                                     │
│                                                                             │
│ Jobs: N parallel                                                            │
│ Counter: constraint_done                                                    │
└─────────────────────────────────────────────────────────────────────────────┘
                                              │
                                              ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ STAGE 10: ISLAND BUILD (T0/T1 ONLY)                                  [SYNC] │
├─────────────────────────────────────────────────────────────────────────────┤
│ read:  constraints[] (body pairs, filtered to T0/T1 bodies)                 │
│ write: islands[] (list of constraint/body index ranges)                     │
│                                                                             │
│ Union-find over T0/T1 constraint graph only.                                │
│ T2–T4 constraints are NOT islanded—they go to the XPBD solver.             │
│ Output: island_count, per-island body/constraint ranges.                    │
│                                                                             │
│ Single job (fast union-find, ~20 µs for T0/T1 constraints).                 │
│ Counter: island_done                                                        │
└─────────────────────────────────────────────────────────────────────────────┘
                                              │
                                              ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ STAGE 11a: TGS SOLVE (T0/T1)                                       [PARALLEL]│
├─────────────────────────────────────────────────────────────────────────────┤
│ read:  islands[], constraints[] (T0/T1), bodies_curr                        │
│ write: velocities_solved[] (T0/T1 bodies)                                   │
│                                                                             │
│ Per-island TGS (Temporal Gauss-Seidel):                                     │
│   for iter in 0..N:                                                         │
│       for constraint in island:                                             │
│           compute impulse, apply to velocities                              │
│                                                                             │
│ Islands are independent → one job per island (or batched small islands).    │
│ Warmstarting: initial impulse from cached manifold lambdas.                 │
│ Typically < 50 near-field bodies.                                           │
│                                                                             │
│ Jobs: M parallel (1 per large island, batched for small islands)            │
│ Counter: tgs_solve_done                                                     │
├─────────────────────────────────────────────────────────────────────────────┤
│ STAGE 11b: JACOBI XPBD SOLVE (T2–T4)                               [PARALLEL]│
├─────────────────────────────────────────────────────────────────────────────┤
│ read:  constraints[] (T2–T4), bodies_curr                                   │
│ write: positions_solved[], velocities_solved[] (T2–T4 bodies)               │
│                                                                             │
│ Jacobi-style XPBD (per-body parallel, no island dependency):                │
│   for iter in 0..K (K=4–8):                                                │
│       for each constraint (Jacobi: read from start-of-iter state):          │
│           Δλ = (-C - α̃·λ) / (∇C^T M^-1 ∇C + α̃)                          │
│           accumulate Δx per body                                            │
│       blend corrections: x += ω · Δx_accumulated  (ω ≈ 0.5–0.8)           │
│                                                                             │
│ Parallelized over bodies, NOT islands. No ordering dependency.              │
│ Unconditionally stable: safe for far-field explosions.                      │
│ Warmstarting: λ_xpbd from manifold cache.                                   │
│                                                                             │
│ Jobs: N parallel (128 bodies/job)                                           │
│ Counter: xpbd_solve_done                                                    │
│                                                                             │
│ NOTE: 11a and 11b can run concurrently (independent body sets).             │
└─────────────────────────────────────────────────────────────────────────────┘
                                              │
                                              ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ STAGE 11c: POSITION PROJECTION + VELOCITY SYNC (T0/T1)                [SYNC] │
├─────────────────────────────────────────────────────────────────────────────┤
│ read:  islands[], constraints[] (T0/T1), bodies_curr                        │
│ write: position_deltas[], bodies_curr (velocity update in place)            │
│                                                                             │
│ Per-island dense position projection (replaces Baumgarte position drift):   │
│   1. Extract Φ(q) from normal constraint rows (penetration - slop)          │
│   2. Assemble A = J M⁻¹ Jᵀ (dense, per-island)                            │
│   3. Solve A · λ = −Φ(q) via LDL^T factorization                           │
│   4. Clamp λ ≥ 0, compute Δq = M⁻¹ Jᵀ λ                                   │
│   5. Velocity sync: replace normal component of v with Δq/dt               │
│      (preserves tangential friction velocity from TGS)                      │
│                                                                             │
│ Sleeping islands produce zero corrections (fast path).                      │
│ All allocations from frame arena.                                           │
│                                                                             │
│ Single job per island (sequential within island, parallel across islands).  │
│ Counter: projection_done                                                    │
└─────────────────────────────────────────────────────────────────────────────┘
                                              │
                                              ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ STAGE 12: INTEGRATE + SLEEP                                        [PARALLEL]│
├─────────────────────────────────────────────────────────────────────────────┤
│ read:  velocities_solved[], bodies_curr (positions)                         │
│ write: bodies_next (positions, orientations), sleep_flags[]                 │
│                                                                             │
│ Semi-implicit Euler:                                                        │
│   position_next = position_curr + velocity * dt                             │
│   orientation_next = integrate_angular(orientation_curr, angular_vel, dt)   │
│                                                                             │
│ Sleep detection:                                                            │
│   if linear_vel < threshold && angular_vel < threshold for K substeps:      │
│       mark as sleeping, skip in future substeps                             │
│                                                                             │
│ Jobs: N parallel (512 bodies/job)                                           │
│ Counter: integrate_done                                                     │
└─────────────────────────────────────────────────────────────────────────────┘
                                              │
                                              ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ STAGE 13: CACHE COMMIT + EVENTS                                       [SYNC] │
├─────────────────────────────────────────────────────────────────────────────┤
│ read:  manifolds[], solver lambda values                                    │
│ write: manifold_cache (persist warmstart data), impact_events[]             │
│                                                                             │
│ Commit solved impulse magnitudes to cache for next frame warmstart.         │
│ Emit impact events for gameplay (damage, sound, fracture triggers).         │
│                                                                             │
│ Single job, ~20 µs.                                                         │
│ Counter: cache_done                                                         │
└─────────────────────────────────────────────────────────────────────────────┘
                                              │
                    ╔═════════════════════════════════════════════════════════╗
                    ║                 END SUBSTEP LOOP                        ║
                    ║         repeat stages 3-13 for remaining substeps       ║
                    ╚═════════════════════════════════════════════════════════╝
                                              │
                                              ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ STAGE 14: BUFFER SWAP                                                 [SYNC] │
├─────────────────────────────────────────────────────────────────────────────┤
│ swap(bodies_curr, bodies_next)                                              │
│                                                                             │
│ Next tick reads from what is now bodies_curr.                               │
└─────────────────────────────────────────────────────────────────────────────┘
                                              │
                                              ▼
                    ┌─────────────────────────────────────────────────────────┐
                    │                     TICK END                            │
                    │  phys_frame_arena reset happens at next tick start      │
                    └─────────────────────────────────────────────────────────┘
```

--------------------------------------------------------------
8.5) JOB DEPENDENCY GRAPH
--------------------------------------------------------------

```
    TICK START
         │
         ▼
    ┌─────────┐
    │ StepPlan│ (single job)
    └────┬────┘
         │
         ▼
    ┌────────────────┐
    │TierClassify    │───────────────────┐
    │(N parallel)    │                   │
    └───────┬────────┘                   │
            │                            │
            ▼                            ▼
    ┌────────────────┐           ┌────────────────┐
    │SpatialUpdate   │           │ (other systems │
    │(N parallel)    │           │  can run here) │
    └───────┬────────┘           └────────────────┘
            │
            │◄──────────── SUBSTEP LOOP ────────────────────┐
            ▼                                               │
    ┌────────────────┐                                      │
    │HaloClosure     │                                      │
    │(small N)       │                                      │
    └───────┬────────┘                                      │
            │                                               │
            ▼                                               │
    ┌────────────────┐                                      │
    │AABBUpdate      │                                      │
    │(N parallel)    │                                      │
    └───────┬────────┘                                      │
            │                                               │
            ▼                                               │
    ┌────────────────┐                                      │
    │Broadphase      │                                      │
    │(N parallel)    │                                      │
    └───────┬────────┘                                      │
            │                                               │
            ▼                                               │
    ┌────────────────┐                                      │
    │Narrowphase     │                                      │
    │(N parallel)    │                                      │
    └───────┬────────┘                                      │
            │                                               │
            ▼                                               │
    ┌────────────────┐                                      │
    │ManifoldBuild   │                                      │
    │(N parallel)    │                                      │
    └───────┬────────┘                                      │
            │                                               │
            ▼                                               │
    ┌────────────────┐                                      │
    │Stabilization   │                                      │
    │(N parallel)    │                                      │
    └───────┬────────┘                                      │
            │                                               │
            ▼                                               │
    ┌────────────────┐                                      │
    │ConstraintBuild │                                      │
    │(N parallel)    │                                      │
    └───────┬────────┘                                      │
            │                                               │
            ▼                                               │
    ┌────────────────┐                                      │
    │IslandBuild     │                                      │
    │(T0/T1 only,   │                                      │
    │ single job)    │                                      │
    └───────┬────────┘                                      │
            │                                               │
            ▼                                               │
    ┌────────────────┐   ┌────────────────┐                 │
    │TGS Solve       │   │XPBD Solve      │                 │
    │(T0/T1,         │   │(T2–T4,         │                 │
    │ M per island)  │   │ N per 128 body) │                 │
    └───────┬────────┘   └───────┬────────┘                 │
            │                    │                          │
            ▼                    │                          │
    ┌────────────────┐           │                          │
    │PosProjection   │           │                          │
    │+ VelocitySync  │           │                          │
    │(T0/T1, per     │           │                          │
    │ island)        │           │                          │
    └───────┬────────┘           │                          │
            │                    │                          │
            └────────┬───────────┘                          │
                     ▼                                      │
    ┌────────────────┐                                      │
    │Integrate+Sleep │                                      │
    │(N parallel)    │                                      │
    └───────┬────────┘                                      │
            │                                               │
            ▼                                               │
    ┌────────────────┐                                      │
    │CacheCommit     │                                      │
    │(single job)    │                                      │
    └───────┬────────┘                                      │
            │                                               │
            └──────────────── if more substeps ─────────────┘
            │
            ▼
    ┌────────────────┐
    │BufferSwap      │
    │(single job)    │
    └───────┬────────┘
            │
            ▼
       TICK END
```

--------------------------------------------------------------
8.6) COUNTER USAGE PATTERN
--------------------------------------------------------------

Each stage uses a job_counter_t for synchronization:

```c
// Example: Broadphase → Narrowphase dependency
job_counter_t broad_done, narrow_done;
job_counter_init(&broad_done, 0);
job_counter_init(&narrow_done, 0);

// Dispatch broadphase jobs
for (int i = 0; i < num_broad_jobs; ++i) {
    job_dispatch(sys, broadphase_job, &broad_args[i], PRIORITY_HIGH, &broad_done);
}

// Wait for broadphase, then dispatch narrowphase
job_wait_counter(&broad_done, 0);
job_counter_destroy(&broad_done);

for (int i = 0; i < num_narrow_jobs; ++i) {
    job_dispatch(sys, narrowphase_job, &narrow_args[i], PRIORITY_HIGH, &narrow_done);
}

job_wait_counter(&narrow_done, 0);
job_counter_destroy(&narrow_done);
```

--------------------------------------------------------------
8.7) MEMORY LAYOUT SUMMARY
--------------------------------------------------------------

```
┌─────────────────────────────────────────────────────────────┐
│                    PERSISTENT (LEVEL ARENA)                 │
├─────────────────────────────────────────────────────────────┤
│ phys_body_pool        [16k × 80 bytes = 1.28 MB]           │
│ phys_aabb_pool        [16k × 24 bytes = 384 KB]            │
│ phys_collider_pool    [16k × 36 bytes = 576 KB]            │
│ manifold_cache        [8k entries × 64 bytes = 512 KB]     │
│   (includes lambda_impulse, lambda_xpbd, solver_mode)     │
│ bodies_curr / bodies_next (double buffer aliased to pool)  │
│ static_bvh            [~1-4 MB depending on world]         │
│ spatial_grid          [cell storage, ~512 KB]              │
├─────────────────────────────────────────────────────────────┤
│                    TOTAL PERSISTENT: ~4-8 MB                │
└─────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│                    TRANSIENT (FRAME ARENA)                  │
├─────────────────────────────────────────────────────────────┤
│ tier_lists[6]         [6 × 16k × 4 bytes = 384 KB max]     │
│ collision_pairs       [~50k pairs × 8 bytes = 400 KB]      │
│ contact_candidates    [~10k × 48 bytes = 480 KB]           │
│ manifolds             [~5k × 64 bytes = 320 KB]            │
│ stab_hints            [~5k × 8 bytes = 40 KB]              │
│ constraints           [~15k × 96 bytes = 1.44 MB]          │
│ islands               [~500 × 16 bytes = 8 KB]             │
│ pos_projection        [per-island: Φ, A, rhs, λ, Δq, Δv]  │
│   (dense nc×nc matrix + vectors, ~few KB per island)       │
│ impact_events         [~1k × 32 bytes = 32 KB]             │
├─────────────────────────────────────────────────────────────┤
│                    TOTAL TRANSIENT: ~3-4 MB                 │
│              (reset each tick, reused next tick)            │
└─────────────────────────────────────────────────────────────┘
```

--------------------------------------------------------------
8.8) NETWORKING INTEGRATION
--------------------------------------------------------------

Physics state replication follows the pure functional model:

```
Server Tick:
  physics_tick() → bodies_next (authoritative)
       │
       ▼
  snapshot_encode(bodies_next) → delta packets
       │
       ▼
  reliable_channel_send() → clients

Client Tick:
  reliable_channel_recv() → delta packets
       │
       ▼
  snapshot_decode() → server_bodies_snapshot
       │
       ▼
  prediction_reconcile(local_bodies, server_snapshot)
       │
       ▼
  physics_tick() with reconciled state
```

Replication uses existing quantization:
- `vec3_mm.h`: 16-bit quantized positions (min/max encoding)
- `quat_snorm16.h`: 16-bit quantized orientations
- Delta compression: only changed bodies sent

--------------------------------------------------------------
8.9) EXTENSION POINTS
--------------------------------------------------------------

The pipeline is designed for incremental feature addition:

JOINTS (implemented):
- Joint pool on phys_world_t: world->joints[], world->joint_count
- Three joint types: DISTANCE (1 row), BALL (3 rows), HINGE (5 rows)
- Joint build functions (joint_ball.c, joint_distance.c, joint_hinge.c)
  produce Jacobian rows each substep, packed into phys_constraint_t via
  phys_joint_build_constraints()
- Joint constraints have is_joint=1, bilateral lambda bounds (push+pull)
- Joints participate in island build and are solved by TGS alongside contacts
- Per-row viscous damping (joint->damping, range 0.1–0.5, applied as
  delta = (bias - jv*(1+damping)) * eff_mass; must stay < 1 for PGS convergence)
- Speed-dependent Baumgarte leak for joints: at high body speeds (5–60 m/s),
  a fraction (0–0.6) of position error leaks into velocity-level bias so the
  solver actively steers anchors together during velocity solve
- Predictive anchor correction (ball joints): blends current and predicted
  (pos + vel*dt) anchor error, blend factor 0–0.5 over 5–80 m/s
- Nonlinear position projection: after TGS iterations, 4 extra passes
  recompute world anchors from predicted body state (pos + pseudo*dt,
  orientation integrated by pseudo angular vel), measure residual error,
  apply coupled position + angular corrections to pseudo-velocities;
  angular correction uses r×e/|r|² to swing lever arms toward targets
- Sub-substep support: fast islands (15–150 m/s) get up to 8 solver
  sub-substeps with inline per-body integration; joint constraints are
  rebuilt each sub-substep but contact constraints stay frozen

ARTICULATED BODIES (future):
- Extend joint system with parent/child links
- Joint limits (angular, linear)
- Reduced-coordinate articulations

FRACTURE (future):
- Impact events feed fracture system
- Fracture generates new bodies/colliders
- Bodies added to pool, spatial index updated next tick

CONTINUOUS COLLISION (future):
- Broadphase detects fast movers
- CCD sweep test before integrate
- Sub-step if tunnel detected
