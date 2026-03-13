OVERVIEW: Tiered TGS/XPBD physics architecture with biomechanical muscle system
(for a fiber-based ECS engine with sparse interactions)

CURRENT IMPLEMENTATION STATUS (source-of-truth: src/physics/world/tick*.c)
- Tick orchestrators run TGS with split impulse; XPBD modules are wired for T2-T4 bodies.
- Position projection / velocity sync are fused into TGS via split impulse (pseudo_velocities[]).
- Halo closure + broadphase run once per tick (outside the substep loop).
- Joint system is implemented: distance, ball, hinge, lock, copy-rotation,
  limit-rotation, limit-position, aim, IK, cone-twist, and twist joints.
- Joint driver system with 5 driver types: motor, spring, linear actuator,
  servo (PD controller), and aero-hydraulic.
- Joint motor system for angular orientation targeting.
- TGS solver has adaptive per-island iteration scaling and sub-substeps.
- Nonlinear joint position projection runs after TGS iterations.
- Sparse Conjugate Gradient (CG) coupled solver for animation-tier joint islands.
- Biomechanical muscle system with Hill force model, tendon dynamics, and
  antagonist muscle pairs for joint actuation.
- Animated entity system maps skeletons to physics bodies with joint constraints.
- Continuous collision detection (CCD) for fast-moving bodies vs static geometry.
- Convex hull collision via GJK/EPA, convex decomposition (V-ACD).
- Compound collider support (tree of child convex hulls).
- Point colliders for skeleton bones needing ground contact.
- Contact-begin and overlap-begin event detection.
- Network snapshot encoding/decoding with quantized body state.
- Client-side prediction with snap/blend reconciliation.
- Deferred command system for thread-safe world mutations.
- Continuous tick runner on dedicated OS thread with pause/resume/step.

==============================================================
1) WHAT THE ARCHITECTURE IS OPTIMIZING FOR
==============================================================

- Server-authoritative physics at 30 Hz.
- Deterministic-leaning, island-based simulation.
- Thousands of "relevant" objects, but a sparse interaction graph.
- Strong gameplay feel:
  - stacks don't creep,
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
    PoseIn / VelIn -> PoseOut / VelOut
- A triple-buffered body pool:
    - bodies_curr: read-only snapshot of the last completed prediction tick.
    - bodies_next: write target for the current prediction tick.
    - bodies_ccd_prev: CCD previous-tick snapshot (rotated in swap).
    - bodies_net: network authority buffer written by the recv thread.
- Network dirty flags are per-slot atomics for lock-free recv -> predict sync.
- Static bodies are just bodies with inv_mass = 0 and PHYS_BODY_FLAG_STATIC.

B) TIERED SIMULATION (T0-T5 + ANIM)
------------------------------------
Tier classification (phys_tier_t, 7 tiers):

  PHYS_TIER_ANIM (0)       -> Animated / ragdoll bodies (TGS coupled, CG solver).
  PHYS_TIER_0_DIRECT (1)   -> Direct manipulation (TGS, full fidelity).
  PHYS_TIER_1_NEAR (2)     -> Near interactive (TGS, full fidelity).
  PHYS_TIER_2_VISIBLE (3)  -> Visible / hazardous (XPBD).
  PHYS_TIER_3_WORLD (4)    -> World-shaping (XPBD).
  PHYS_TIER_4_BACKGROUND (5) -> Background dynamic (XPBD).
  PHYS_TIER_5_SLEEPING (6) -> Sleeping / dormant (skipped).

Tier classification is distance-based from player positions, camera FOV,
and manipulation state (phys_game_state_t input each tick).

ANIM is the lowest numeric value so it wins min-tier island promotion,
pulling the whole island to TGS with coupled implicit velocity solve.

Tier lists are arena-allocated packed index arrays, rebuilt each tick.

C) DUAL SOLVER
--------------
- TGS (Temporal Gauss-Seidel) for T0/T1 and ANIM-tier bodies:
    - Per-island sequential impulse solving.
    - Split impulse for position correction (pseudo_velocities[]).
    - Warmstarting from cached accumulated impulses.
    - Adaptive per-island iteration scaling.
    - Friction cone clamping (Coulomb model).
- XPBD (Extended Position-Based Dynamics) for T2-T4 bodies:
    - Position-level constraint projection.
    - Unconditionally stable, does not require island decomposition.
    - Jacobi relaxation factor for parallel convergence.
    - Per-constraint compliance parameter.
- CG (Conjugate Gradient) coupled solver for ANIM-tier joints:
    - Assembles full J*M^-1*J^T system matrix in sparse CSR.
    - Projected CG with Jacobi preconditioning.
    - Handles box constraints (lambda clamping) and friction cones.
    - Updates positions and orientations during solve (coupled integration).
    - Predicts body positions from velocity for Jacobian rebuild.

D) ISLAND DECOMPOSITION
------------------------
- Union-find connected components from constraint pairs.
- Per-island body and constraint index lists (arena-allocated).
- Sleeping islands are skipped entirely.
- Islands can be capped at max_island_bodies for splitting.
- Graph coloring for parallel constraint solving within large islands:
    - Greedy lowest-degree-first algorithm.
    - Constraints of the same color solved in parallel without write contention.
    - Configurable threshold (island_color_threshold) to skip small islands.

E) CONSTRAINT COLORING
-----------------------
- Greedy lowest-degree-first graph coloring.
- Two constraints are adjacent if they share a body.
- All workspace is arena-allocated (no malloc on hot path).
- Uses scratch buffer, no heap allocations during solve.

==============================================================
3) RIGID BODY STATE
==============================================================

phys_body_t (160 bytes, static-asserted):
  - position: phys_vec3_t
  - orientation: phys_quat_t
  - linear_vel: phys_vec3_t
  - angular_vel: phys_vec3_t
  - inv_mass: float
  - inv_inertia_diag: phys_vec3_t (diagonal inverse inertia)
  - flags: uint32_t (bitmask of PHYS_BODY_FLAG_*)
  - tier: uint8_t (current simulation tier)
  - sleep_counter: uint8_t (consecutive low-velocity frames)
  - friction: float (surface friction coefficient)
  - restitution: float (coefficient of restitution)
  - linear_damping: float (mass-dependent velocity damping)
  - angular_damping: float (mass-independent angular damping)
  - entity_index: uint32_t (ECS entity owner, UINT32_MAX = unlinked)
  - world_transform: mat4_t (authoritative rendering state, column-major)

Body flags:
  PHYS_BODY_FLAG_STATIC (1<<0)          - Zero inverse mass, immovable.
  PHYS_BODY_FLAG_KINEMATIC (1<<1)       - Script/animation-driven position.
  PHYS_BODY_FLAG_SLEEPING (1<<2)        - Below velocity threshold, skipped.
  PHYS_BODY_FLAG_CCD (1<<3)            - Enable swept CCD vs static mesh.
  PHYS_BODY_FLAG_CONTACT_RESTING (1<<4) - Has contact support opposing gravity.
  PHYS_BODY_FLAG_TRIGGER (1<<5)         - Detect contacts but skip solver response.
  PHYS_BODY_FLAG_NO_BROADPHASE (1<<6)   - Skip broadphase; joint-only body.
  PHYS_BODY_FLAG_NO_GRAVITY (1<<7)      - Disable gravity integration.
  PHYS_BODY_FLAG_HAD_CONTACT (1<<8)     - Had contact constraint last substep.

Inertia helpers:
  - phys_body_set_box_inertia()
  - phys_body_set_sphere_inertia()
  - phys_body_set_capsule_inertia()

Damping model:
  Linear:  v_new = v / (1 + c * inv_mass * dt)  (mass-dependent)
  Angular: w_new = w / (1 + c * dt)  (mass-independent)
  Integrated via implicit Euler for unconditional stability.

==============================================================
4) COLLIDER SHAPES
==============================================================

phys_shape_type_t discriminator (9 types):
  PHYS_SHAPE_SPHERE (0)    -> phys_sphere_t { radius }
  PHYS_SHAPE_BOX (1)       -> phys_box_t { half_extents }
  PHYS_SHAPE_CAPSULE (2)   -> phys_capsule_t { radius, half_height }
  PHYS_SHAPE_COMPOUND (3)  -> phys_convex_compound_t (tree of child convex hulls)
  PHYS_SHAPE_CONVEX (4)    -> phys_convex_hull_t (<=64 verts, <=64 faces)
  PHYS_SHAPE_MESH (5)      -> phys_mesh_shape_t { triangles, tri_count, solid, bvh }
  PHYS_SHAPE_HALFSPACE (6) -> phys_halfspace_t { normal, distance }
  PHYS_SHAPE_POINT (7)     -> Zero-volume point (body center contact)
  PHYS_SHAPE_COUNT (8)

phys_collider_t (40 bytes, static-asserted):
  - type: phys_shape_type_t (discriminator)
  - shape_index: uint32_t (index into shape-specific pool)
  - local_offset: phys_vec3_t (offset from body origin)
  - local_rotation: phys_quat_t (rotation relative to body)
  - sphere_simplify: uint8_t (eligible for bounding-sphere approximation at T2+)
  - layer_id: uint8_t (query layer index 0-31)

Shape pools are stored on the world:
  spheres[], boxes[], capsules[], meshes[], convex_hulls[],
  halfspaces[], compounds[]

Convex hull details:
  - Max 64 vertices, 64 faces, 384 face indices.
  - Accepts up to 8192 input points, produces output hull.
  - Incremental convex hull builder.
  - GJK/EPA compatible via support function.
  - Local-space AABB and centroid precomputed.

Convex decomposition (V-ACD):
  - Voxelize mesh surface + flood-fill interior.
  - Iteratively split concave regions along max-concavity plane.
  - Build convex hull of each piece's voxel centers.
  - Max 64 output hulls, configurable resolution (8-128 per axis).

Mesh collider:
  - Triangle array + BVH (caller-owned, borrowed pointers).
  - Solid flag for closed mesh (backface push direction).
  - BVH supports AABB query and raycast.
  - Should only be attached to static bodies.

==============================================================
5) BROADPHASE
==============================================================

A) SPATIAL HASH GRID (dynamic bodies)
--------------------------------------
- Hash table of cells (power-of-2 count).
- Bodies inserted by AABB; queries return all overlapping body indices.
- Arena-allocated cell arrays (no individual frees).
- Duplicate insertions into same cell suppressed.
- Configurable cell size and cell count.

B) STATIC BVH (static geometry)
--------------------------------
- SAH-based (Surface Area Heuristic) top-down BVH with binning.
- Built once at initialization (or when invalidated), stored persistently.
- Separate arena for static BVH memory.
- Supports AABB overlap query and slab-based raycast.
- Bucket flag precomputation: rasterizes BVH leaves to spatial grid hash
  buckets so broadphase can skip BVH queries for buckets without static geometry.
- Invalidated when static bodies are added/removed.

==============================================================
6) NARROWPHASE
==============================================================

Shape-pair intersection tests (all support speculative contacts):

  Sphere vs Sphere          (narrowphase_sphere.c)
  Sphere vs Box             (narrowphase_sphere_box.c)
  Sphere vs Capsule         (narrowphase_sphere_capsule.c)
  Sphere vs Triangle        (narrowphase_sphere_tri.c)
  Box vs Box                (box_box_sat.c — SAT algorithm)
  Box vs Capsule            (narrowphase_box_capsule.c)
  Box vs Triangle           (narrowphase_box_tri.c)
  Capsule vs Capsule        (narrowphase_capsule_capsule.c)
  Capsule vs Triangle       (narrowphase_capsule_tri.c)
  Halfspace vs primitives   (narrowphase_halfspace.c)
  Halfspace vs Point        (narrowphase_halfspace_point.c)
  Convex vs Convex          (narrowphase_convex.c — GJK/EPA)
  Mesh vs primitives        (narrowphase_mesh.c — BVH traversal + per-tri tests)

GJK/EPA (gjk.c, epa.c):
  - Support function callback abstraction for any convex shape.
  - GJK returns intersection test + closest points when separated.
  - EPA expands GJK simplex to find penetration depth and MTV.
  - Support functions for sphere, box, capsule, convex hull (gjk_support.c).

Sphere simplification:
  - Bodies with sphere_simplify flag use bounding-sphere narrowphase at T2+.
  - Flag set at load time when circumradius/inradius ratio < 1.3.

Contact candidates (phys_contact_candidate_t):
  - Up to 4 contact points per pair (PHYS_MAX_MANIFOLD_POINTS).
  - Per-pair skip flags for CCD-handled pairs.

==============================================================
7) CONTACT MANIFOLDS
==============================================================

phys_contact_point_t:
  - point_world: world-space contact point
  - local_a / local_b: body-local contact points
  - normal: contact normal (A -> B)
  - penetration: positive = overlap
  - feature_id: persistent tracking (edge, face, or vertex)

phys_manifold_t (up to 4 points per body pair):
  - body_a, body_b indices
  - point_count (0-4)
  - points[4]
  - friction / restitution (combined material properties)
  - Warmstarting data: normal_impulse[4], tangent_impulse[4][2]

Material combination:
  - Friction: geometric mean sqrt(f1 * f2)
  - Restitution: minimum min(r1, r2)

Manifold reduction:
  - When exceeding 4 points, reduces to best-spread 4.
  - Feature IDs for persistent contact tracking:
      Edge: (face << 8) | edge
      Face: 0x10000 | face
      Vertex: 0x20000 | vertex

==============================================================
8) MANIFOLD CACHE (WARMSTARTING)
==============================================================

phys_manifold_cache_t:
  - Open-addressing hash table with linear probing.
  - Keyed by body pair (smaller ID first), normalized internally.
  - O(1) average lookup.
  - Entries store manifold + last_used_tick for expiry.
  - Cache expiry: entries not used within max_age ticks are removed.
  - Compaction on removal via swap-with-last.
  - Warmstart decay: configurable impulse decay per cache commit.

==============================================================
9) CONSTRAINT SOLVER
==============================================================

A) JACOBIAN ROWS
-----------------
phys_jacobian_row_t:
  - J_va, J_wa: linear + angular Jacobian for body A
  - J_vb, J_wb: linear + angular Jacobian for body B
  - effective_mass: 1 / (J * M^-1 * J^T)
  - bias: Baumgarte stabilization + restitution bias
  - constraint_error: raw position/angle error for XPBD
  - lambda: accumulated impulse (warmstarted)
  - lambda_min / lambda_max: clamp bounds
  - pseudo_lambda: split-impulse position correction
  - damping: velocity damping coefficient
  - flags: PHYS_ROW_FLAG_ANGULAR (0x01), PHYS_ROW_FLAG_DRIVE (0x02)

Row flags:
  - ANGULAR: reduced Baumgarte leak and split-impulse ERP.
  - DRIVE: uses drive_compliance instead of constraint compliance.

phys_constraint_t (up to 9 rows):
  - body_a, body_b indices
  - manifold_idx / point_idx for warmstart writeback
  - row_count
  - solver_mode: 0=TGS, 1=XPBD
  - is_joint: 0=contact, 1=animation joint (soft), 2=structural joint (hard)
  - friction, penetration
  - compliance, angular_compliance, drive_compliance, joint_damping
  - rows[PHYS_MAX_CONSTRAINT_ROWS (9)]

Contact constraint:
  - 3 rows: 1 normal + 2 friction tangent.
  - Baumgarte bias with penetration slop.
  - Coulomb friction cone clamping.

B) TGS SOLVER (phys_stage_tgs_solve)
-------------------------------------
- Per-island sequential impulse.
- Initializes velocity workspace from body state.
- Iterates constraint rows, updating velocities with impulse corrections.
- Split impulse: pseudo_velocities for position correction (decoupled from velocity).
- Warmstarting from cached lambdas.
- Configurable iterations (typically 20-24).
- Per-tier substep counts.
- Supports mutable body array for coupled CG solver (ANIM tier).

C) XPBD SOLVER (phys_stage_xpbd_solve)
---------------------------------------
- Position-level constraint projection.
- Copies body positions from bodies_in to bodies_out.
- Iterates position-level corrections.
- Velocities derived from position delta / dt.
- Jacobi relaxation factor (omega 0.5-0.8) for parallel convergence.
- Per-constraint compliance parameter.
- Supports constraint rebuild between iterations (joints and contacts).
- Parallel batch solve: disjoint constraint slices on multiple fibers.

D) CG COUPLED SOLVER (solver/cg/)
----------------------------------
- For ANIM-tier islands with joint constraints.
- Assembles sparse A = J*M^-1*J^T + regularization in CSR format.
- Max 512 rows per island (CG_MAX_ROWS).
- Jacobi-preconditioned projected CG with box constraints.
- Friction cone projection during CG iterations.
- Applies solution as velocity + position corrections.
- Predicts positions from full velocity for Jacobian rebuild.
- Falls back to Gauss-Seidel if NNZ overflow occurs during assembly.

E) POSITION PROJECTION
-----------------------
- Sparse per-island position projection (replaces Baumgarte for stiff contacts).
- Assembles A = J*M^-1*J^T for normal constraint rows only.
- Dense LDL^T factorization and solve.
- Computes generalized position corrections (translation + orientation).
- Friction rows are unaffected.

F) VELOCITY SYNC
-----------------
- Synchronizes velocity workspace after position corrections.

==============================================================
10) JOINT SYSTEM
==============================================================

A) JOINT TYPES (phys_joint_type_t, 11 types)
---------------------------------------------

  PHYS_JOINT_DISTANCE (0)       -> 1 row: spring-damper distance constraint.
  PHYS_JOINT_BALL (1)           -> 3 rows: 3-DOF rotation (positional lock).
  PHYS_JOINT_HINGE (2)          -> 5 rows: 1-DOF rotation (3 pos + 2 ang lock).
  PHYS_JOINT_LOCK (3)           -> 6 rows: 0-DOF full rigid attachment.
  PHYS_JOINT_COPY_ROTATION (4)  -> 3 rows: match orientation (angular only).
  PHYS_JOINT_LIMIT_ROTATION (5) -> up to 3 rows: per-axis angular limits.
  PHYS_JOINT_LIMIT_POSITION (6) -> up to 3 rows: per-axis positional limits.
  PHYS_JOINT_AIM (7)            -> 2 rows: align axis toward target.
  PHYS_JOINT_IK (8)             -> 3 rows: IK chain pair angular steering.
  PHYS_JOINT_CONE_TWIST (9)     -> 3-6 rows: ball + per-axis angular limits.
  PHYS_JOINT_TWIST (10)         -> 5-6 rows: single-axis twist with optional limit.

Max rows per joint: PHYS_JOINT_MAX_ROWS (9).

phys_joint_t includes:
  - type, body_a, body_b
  - local_anchor_a/b, local_axis_a
  - Distance params: rest_length, spring_stiffness, spring_damping
  - Limit params: limit_min[3], limit_max[3], limit_axes bitmask
  - rest_relative_orient: reference orientation for angular limits
  - Aim: track_axis (local axis on body_b to aim)
  - IK: ik_ee_body, ik_target_body, ik_target_pos
  - Warmstarting: cached_lambda[PHYS_JOINT_MAX_ROWS]
  - Compliance: compliance, angular_compliance, drive_compliance
  - Damping: damping coefficient
  - Yield/break: accumulated_impulse, yield_strength, break_strength, broken flag
  - Joint flags: PHYS_JOINT_FLAG_ANGULAR_DRIVE, PHYS_JOINT_FLAG_LINEAR_DRIVE
  - mass_scale: CG solver inertia scaling factor
  - Solver output: row_count, rows[PHYS_JOINT_MAX_ROWS]

Joint -> constraint conversion:
  phys_joint_build_constraints() packs joint rows into 1-2 phys_constraint_t
  entries (max 9 rows each via PHYS_MAX_CONSTRAINT_ROWS).

B) JOINT DRIVERS (phys_joint_driver_t, 5 types)
------------------------------------------------
Modifies a specific constraint row on a built joint, overriding bias
and lambda bounds for actuation behavior:

  PHYS_DRIVER_NONE (0)             -> No driver (default).
  PHYS_DRIVER_MOTOR (1)            -> Constant-velocity motor.
    Params: target_velocity, max_force.
  PHYS_DRIVER_SPRING (2)           -> Restoring spring with damping.
    Params: stiffness, damping, rest_value.
  PHYS_DRIVER_LINEAR_ACTUATOR (3)  -> Position-targeting with speed limit.
    Params: target_position, max_speed, max_force.
  PHYS_DRIVER_SERVO (4)            -> PD controller.
    Params: target_value, kp, kd, max_force.
  PHYS_DRIVER_AERO_HYDRAULIC (5)   -> Velocity-dependent drag/flow-limited force.
    Params: drag_coeff, flow_limit, max_force.

Usage: build joint rows first, then apply driver to target row.

C) JOINT MOTOR (phys_joint_motor_t)
------------------------------------
Angular motor that drives body B toward a target orientation:
  - target_orientation: target world orientation for body B.
  - strength: 0.0-1.0 (0 = passive, 1 = near-kinematic).
  - max_torque: lambda clamp magnitude.
  - Adds up to 3 angular rows to an already-built joint.

D) ANGULAR PROJECTION
-----------------------
Nonlinear joint angular position projection runs after TGS iterations
to correct angular drift in joint constraints.

==============================================================
11) BIOMECHANICAL MUSCLE SYSTEM
==============================================================

A comprehensive Hill-type muscle model for biologically-inspired
joint actuation. Located in src/physics/muscle/ and
include/ferrum/physics/muscle/.

A) ACTIVATION DYNAMICS (activation.h)
--------------------------------------
First-order ODE modeling neural-to-mechanical delay:
  da/dt = (u - a) / tau
  tau = tau_rise when u > a, tau_fall when u < a.
Integrated with semi-implicit Euler. Clamped to [0,1].
Default: tau_rise=0.015s, tau_fall=0.050s.

B) HILL FORCE MODEL (force_curve.h)
-------------------------------------
F_total = activation * f_active(L) * f_velocity(V) * max_force
        + f_passive(L) * max_force

Three curves:
  - Active force-length: Gaussian-like peak at optimal fiber length.
  - Passive force-length: exponential rise at long lengths.
  - Force-velocity: Hill equation (concentric drops, eccentric rises).

Parameters per muscle:
  - optimal_length, max_force, max_velocity
  - pennation_angle, width (force-length curve breadth)

C) TENDON SERIES ELASTIC ELEMENT (tendon.h)
---------------------------------------------
Stiff nonlinear spring in series with muscle fiber:
  F_muscle(L_fiber) = F_tendon(L_tendon)
  L_tendon = L_total - L_fiber * cos(pennation)

Solved via bounded Newton iteration (max 10 iterations).
Enables energy storage (e.g. Achilles tendon in running).

Parameters: slack_length, stiffness, reference_strain.

D) ATTACHMENT GEOMETRY (geometry.h)
------------------------------------
Origin/insertion attachment points in bone-local coordinates.
Optional cylinder wrapping for muscles crossing bony prominences.
Computes moment arm and fiber length from body transforms.
  Torque = muscle_force * moment_arm

E) MUSCLE UNIT (muscle_unit.h)
-------------------------------
Composite evaluation pipeline:
  1. Step activation dynamics.
  2. Compute geometry (moment arm, fiber length).
  3. Solve tendon equilibrium.
  4. Compute Hill force.
  5. Torque = force * moment_arm.

Tracks fiber_length and fiber_velocity between evaluations.

F) ANTAGONIST MUSCLE PAIR (muscle_pair.h)
------------------------------------------
Pairs flexor + extensor on opposite sides of a joint:
  net_torque = flexor_torque - extensor_torque
  co-contraction stiffness = |flexor_torque| + |extensor_torque|

Targets a specific joint row index for constraint actuation.

==============================================================
12) CONTINUOUS COLLISION DETECTION (CCD)
==============================================================

Opt-in per body via PHYS_BODY_FLAG_CCD or automatic via auto_ccd_speed
threshold (bodies moving faster than threshold are auto-marked).

Supported shapes: sphere, capsule, box.

Tests:
  - Ray vs triangle (Moller-Trumbore).
  - Swept sphere vs single triangle (earliest TOI in [0,1]).
  - Swept sphere vs mesh BVH (builds swept AABB, queries BVH, tests triangles).

CCD stage (phys_stage_ccd):
  - Runs after integration.
  - For each CCD body with displacement > bounding radius:
    sweep-tests against all static mesh shapes.
  - On hit: clamp position to TOI, project/zero velocity along contact normal.
  - Returns number of bodies clamped.

CCD statics stage:
  - CCD for dynamic bodies vs static halfspace/convex/compound shapes.

==============================================================
13) PHYSICS TICK PIPELINE
==============================================================

Pipeline stages (phys_stage_id_t, 16 stages):

  0. STEP_PLAN         -> Compute per-tick simulation parameters.
  1. TIER_CLASSIFY     -> Assign bodies to tiers based on game state.
  2. SPATIAL_UPDATE    -> Update spatial grid for dynamic bodies.
  3. HALO_CLOSURE      -> Propagate tier upgrades to neighbors.
  4. AABB_UPDATE       -> Recompute body AABBs from colliders.
  5. BROADPHASE        -> Spatial grid + static BVH pair generation.
  6. NARROWPHASE       -> Shape-specific intersection tests.
  7. MANIFOLD_BUILD    -> Build/update contact manifolds from candidates.
  8. STABILIZATION     -> Tier-based stabilization hints.
  9. CONSTRAINT_BUILD  -> Build solver constraints from manifolds + joints.
 10. ISLAND_BUILD      -> Union-find island decomposition.
 11. TGS_SOLVE         -> Temporal Gauss-Seidel velocity solver (T0/T1/ANIM).
 12. XPBD_SOLVE        -> Position-based solver (T2-T4).
 13. INTEGRATE         -> Position/orientation integration from velocities.
 14. CACHE_COMMIT      -> Commit manifold cache, emit impact events.
 15. COLLISION_FUSED   -> Fused narrow->manifold->stab->constraint (optimization).

Each substep loop:
  1. (Optional) Animation substep callback.
  2. Broadphase (once per tick, outside substep loop).
  3. Narrowphase -> Manifold build -> Stabilization -> Constraint build.
  4. Island build.
  5. TGS solve (T0/T1/ANIM islands) and XPBD solve (T2-T4 constraints).
  6. Integrate positions and orientations.
  7. CCD pass (clamp fast bodies that swept through static geometry).
  8. Cache commit (end of substep).

Prediction mode:
  When world->prediction_mode is set, narrowphase collision response
  (stages 6-11) is skipped. Bodies still integrate under gravity and
  broadphase still runs. Used on the client for server-authoritative
  prediction.

==============================================================
14) PARALLEL PHYSICS (JOB SYSTEM)
==============================================================

phys_job_context_t wraps the engine job system with per-stage counters.

Dispatch model:
  - phys_dispatch_stage() splits items into batches and dispatches.
  - phys_wait_stage() spins briefly then parks the fiber.
  - phys_batch_size() targets ~(2 * worker_count) jobs.
  - Each batch receives a phys_job_batch_t with start/count/user_args.

Parallelized stages (src/physics/stages/par/):
  - broadphase_par.c
  - narrowphase_par.c
  - manifold_build_par.c
  - constraint_build_par.c
  - integrate_par.c
  - spatial_update_par.c
  - stabilization_par.c
  - tgs_solve_par.c (per-island parallel solve)
  - xpbd_solve_par.c (per-batch parallel XPBD)
  - tier_classify_par.c
  - collision_fused_par.c

Sync stages (run on calling fiber):
  - step_plan
  - island_build
  - cache_commit

==============================================================
15) MEMORY MANAGEMENT
==============================================================

A) FRAME ARENA (phys_frame_arena_t)
-------------------------------------
- Bump-pointer allocator wrapping arena_t.
- Owns backing buffer (malloc'd on init, freed on destroy).
- Reset is O(1) — invalidates all pointers.
- Used for transient per-tick allocations:
    contact manifolds, island lists, tier lists,
    constraint arrays, coloring workspace, etc.

B) BODY POOL (phys_body_pool_t)
---------------------------------
- Triple-buffered: curr, next, ccd_prev, net.
- Per-slot active flags.
- Atomic net_dirty flags for lock-free network sync.
- O(1) buffer swap (pointer exchange).

C) GENERAL POOLS
-----------------
Shape pools (sphere, box, capsule, mesh, convex hull, halfspace, compound)
are flat arrays on the world struct with count tracking.

==============================================================
16) NETWORKING
==============================================================

A) SNAPSHOT ENCODING (snapshot.h)
----------------------------------
phys_snapshot_body_t (26 bytes per body):
  - position[3]: int16 quantized to mm (+-32m range)
  - orientation[3]: smallest-3 quaternion (snorm16)
  - linear_vel[3]: int16 quantized (+-32 m/s range)
  - angular_vel[3]: int16 quantized
  - flags: uint8_t (sleeping, kinematic, etc)
  - tier: uint8_t

Wire format: [tick:8][body_count:4][body_0:26][body_1:26]...

Quantization:
  - Vec3: scale + clamp to int16.
  - Quaternion: smallest-3 encoding (largest component index in high 2 bits of out[0]).

B) PREDICTION (prediction.h)
-------------------------------
Client-side prediction reconciliation:
  - Per-body comparison of local vs server state.
  - Position error: Euclidean distance.
  - Rotation error: 2*acos(|dot(q1,q2)|).
  - Snap: if either error exceeds snap threshold (default 0.5).
  - Blend: lerp/slerp for small errors (default blend rate 0.1).

phys_prediction_result_t reports:
  bodies_snapped, bodies_blended, bodies_correct,
  max_position_error, max_rotation_error.

C) COMMAND SYSTEM (phys_cmd.h)
-------------------------------
Deferred world mutations via topic channel:

Command types:
  PHYS_CMD_SPAWN_BODY (1)     -> Create body with position, velocity, mass, shape.
  PHYS_CMD_SET_POSITION (2)   -> Kinematic teleport.
  PHYS_CMD_APPLY_IMPULSE (3)  -> Apply impulse to linear velocity.
  PHYS_CMD_DESTROY_BODY (4)   -> Remove body.
  PHYS_CMD_SET_STATE (5)      -> Full state set (selective fields via bitmask).
  PHYS_CMD_ADD_JOINT (6)      -> Add joint between two bodies.
  PHYS_CMD_SET_MATERIAL (7)   -> Set friction and restitution.

Thread-safe operation:
  - Commands pushed from main thread via topic channel spinlock.
  - phys_cmd_drain_spawns() separates spawns (immediate) from mutations (deferred).
  - phys_cmd_apply_mutations() writes to bodies_next before buffer swap.
  - Spawn callback for mapping user_tag to assigned body_index.

D) TICK RUNNER (phys_tick_runner.h)
------------------------------------
Continuous tick runner on dedicated OS thread:
  1. Drain spawn/mutation commands.
  2. Pre-tick callback (animation, kinematic body update).
  3. phys_world_tick_parallel().
  4. Drain corrections.
  5. Post-tick callback.
  6. Signal completion (atomic counter).
  7. Sleep until next tick interval.

Features:
  - Pause/resume/step-once for debugging.
  - no_pacing mode for benchmarks.
  - Tick duration and completion timestamp tracking (atomic reads).
  - Backward-compatible kick/done/consume API.

==============================================================
17) ANIMATED ENTITIES
==============================================================

phys_anim_entity_t maps a skeleton to physics world bodies:
  - One body per bone (with collider, mass/inertia from skeleton's bone_collider_desc_t).
  - One joint per parent-child bone pair (from skeleton's bone_joint_desc_t).
  - Collision exclusion pairs for connected/overlapping bones.

API:
  - phys_anim_entity_create(): creates bodies + joints from skeleton definition.
  - phys_anim_entity_sync_from_world(): reads solved body positions back to bone matrices.
  - phys_anim_entity_push_kinematic(): updates kinematic body positions from animation.
  - phys_anim_entity_drive_toward(): blended drive of all bodies toward animation targets.
  - phys_anim_entity_destroy(): frees entity arrays (does NOT remove bodies/joints from world).

Head offset tracking: body-local offset from midpoint to bone head position
for recovering correct bone HEAD position from body midpoint during skinning.

Non-body bones (no collider): inherit world transform from parent * rest_local,
so entire skeleton moves coherently.

Animation substep callbacks:
  - phys_anim_substep_fn: called at start of each substep, before solver.
  - phys_anim_integrate_fn: called during integrate for dynamic anim-tier bodies.

==============================================================
18) WORLD QUERIES
==============================================================

A) RAYCAST (raycast.h)
-----------------------
  - phys_raycast(): single closest hit with layer mask filter.
  - phys_raycast_all(): collect up to N hits, sorted by distance.
  - Layer mask: body tested when (mask & (1 << collider.layer_id)) != 0.
  - Tests against all active bodies via broadphase culling.

B) OVERLAP (overlap.h)
-----------------------
  - phys_overlap(): test a shape at a given pose against all world bodies.
  - Returns array of overlapping body IDs.
  - Layer mask filtering.

C) CLOSEST POINT (closest_point.h)
------------------------------------
  - phys_closest_point(): find closest surface point to a query point.
  - Broadphase culling via spatial grid.
  - Maximum distance threshold.
  - Layer mask filtering.

==============================================================
19) CONTACT/OVERLAP EVENT DETECTION
==============================================================

A) CONTACT-BEGIN (phys_contact_begin.h)
----------------------------------------
  - Tracks body pairs with active contacts across ticks.
  - Detects new contact-begin events from the manifold cache.
  - Events include: body pair, contact point, normal, impulse.
  - Pair tracking set prunes pairs not seen on current tick.

B) OVERLAP-BEGIN (phys_overlap_begin.h)
----------------------------------------
  - Similar tracking for overlap/trigger events.

C) IMPACT EVENTS (world.h, cache_commit.h)
--------------------------------------------
  - Impact event buffer filled during cache_commit stage.
  - Configurable impulse threshold for emission.
  - Per-body and strongest-impact query API.

==============================================================
20) COLLISION EXCLUSION
==============================================================

phys_pair_set_t hash set of excluded body pairs:
  - Excluded pairs skipped during narrowphase.
  - Used for animation bones with overlapping bind-pose colliders
    or directly connected by joints.
  - phys_world_exclude_pair() / phys_world_is_pair_excluded().

==============================================================
21) GAME STATE INPUT
==============================================================

phys_game_state_t provides gameplay context each tick:
  - Players (up to 16): position, velocity, look direction,
    interaction radius, manipulation state.
  - Camera: position, forward, FOV.
  - Hazard indices (up to 64): bodies marked hazardous.
  - Time: game_time, time_scale.

Query functions:
  - phys_game_state_is_manipulated(): check if body is grabbed by a player.
  - phys_game_state_distance_to_nearest_player(): distance-based tier classification.
  - phys_game_state_is_in_view(): camera FOV cone test.

==============================================================
22) STEP PLAN
==============================================================

phys_step_plan_t computed once per tick:
  - substeps, solver_iterations
  - dt, substep_dt
  - Per-tier params (active, solver_mode, substeps, iterations,
    compliance, friction_boost, restitution_scale)

Solver mode selection:
  - TGS for T0/T1/ANIM tiers.
  - XPBD for T2-T4 tiers.
  - Cross-tier: TGS if either body is T0/T1, otherwise XPBD.

==============================================================
23) WORLD CONFIGURATION
==============================================================

phys_world_config_t:
  - max_bodies, max_colliders
  - manifold_cache_size
  - frame_arena_size
  - fixed_dt (seconds)
  - gravity vector
  - default_substeps, default_solver_iterations
  - baumgarte (stabilization factor)
  - slop (penetration slop)
  - sleep_threshold_linear, sleep_threshold_angular, sleep_delay_frames
  - warmstart_decay (impulse decay per cache commit)
  - velocity_damping (fraction retained per second)
  - island_color_threshold (min constraints for graph coloring)
  - speculative_margin (max separation for speculative contacts)
  - max_island_bodies (for island splitting)
  - max_joints
  - max_dt_override (variable timestep multiplier)
  - auto_ccd_speed (speed threshold for automatic CCD)
  - xpbd_min_compliance (minimum compliance floor for XPBD joints)

==============================================================
24) FILE STRUCTURE
==============================================================

Headers: include/ferrum/physics/
  body.h, collider.h, constraint.h, joint.h, joint_driver.h,
  joint_motor.h, manifold.h, manifold_cache.h, narrowphase.h,
  spatial_grid.h, static_bvh.h, island.h, tgs_solve.h,
  xpbd_solve.h, position_projection.h, velocity_sync.h,
  step_plan.h, tier_list.h, tier_classify.h, broadphase.h,
  integrate.h, stabilization.h, aabb.h, aabb_update.h, ccd.h,
  ccd_dynamic.h, gjk_epa.h, gjk_support.h, convex_hull.h,
  convex_decompose.h, convex_compound.h, compound_collider.h,
  mesh_collider.h, mesh_narrowphase.h, phys_pool.h, phys_jobs.h,
  phys_types.h, phys_vec3.h, phys_quat.h, phys_mat3.h,
  world.h, tick.h, phys_tick_runner.h, phys_cmd.h,
  prediction.h, snapshot.h, game_state.h, query.h, raycast.h,
  overlap.h, closest_point.h, phys_anim_entity.h,
  phys_contact_begin.h, phys_overlap_begin.h, phys_overlap.h,
  phys_pair_set.h, cache_commit.h, constraint_color.h,
  constraint_rebuild.h, constraint_stage.h, halo_closure.h,
  island_build.h, island_tier_promote.h, manifold_build.h,
  occlusion_nudge.h, spatial_update.h, amortized.h,
  solver_transition.h, joint_angular_projection.h

  collision/: box_box.h, box_capsule.h, capsule_capsule.h,
              halfspace.h, sphere_simplify.h

  muscle/: activation.h, force_curve.h, tendon.h, geometry.h,
           muscle_unit.h, muscle_pair.h

  solver/: cg_types.h, cg_solve.h

  par/: broadphase_par.h, narrowphase_par.h, manifold_build_par.h,
        constraint_build_par.h, integrate_par.h, spatial_update_par.h,
        stabilization_par.h, tgs_solve_par.h, xpbd_solve_par.h,
        tier_classify_par.h, collision_fused_par.h

Sources: src/physics/
  body/: body_core.c, body_flags.c, body_inertia_box.c,
         body_inertia_capsule.c, body_inertia_sphere.c,
         body_trigger.c, phys_mat3_ops.c

  broadphase/: spatial_grid.c, static_bvh.c, static_bvh_raycast.c

  collider/: collider_init.c, collider_transform.c,
             compound_collider.c, convex_acd.c, convex_decompose.c,
             convex_hull_build.c, convex_hull_support.c,
             convex_voxelize.c, mesh_collider_build.c,
             mesh_collider_query.c

  collision/: aabb_ops.c, aabb_query.c, aabb_shapes.c,
              box_box_sat.c, ccd_dynamic.c, epa.c, gjk.c,
              gjk_support.c, manifold_cache.c, manifold_cache_ops.c,
              manifold_core.c, manifold_feature.c, manifold_feature_id.c,
              manifold_material.c, manifold_reduce.c,
              narrowphase_box_capsule.c, narrowphase_box_tri.c,
              narrowphase_capsule_capsule.c, narrowphase_capsule_tri.c,
              narrowphase_convex.c, narrowphase_halfspace.c,
              narrowphase_halfspace_point.c, narrowphase_mesh.c,
              narrowphase_sphere.c, narrowphase_sphere_box.c,
              narrowphase_sphere_capsule.c, narrowphase_sphere_tri.c,
              phys_contact_begin.c, phys_overlap_begin.c, phys_overlap.c,
              phys_pair_set.c, phys_pair_set_gc.c, sphere_simplify.c

  constraint/: joint_aim.c, joint_ball.c, joint_cone_twist.c,
               joint_constraint.c, joint_copy_rotation.c,
               joint_distance.c, joint_driver.c, joint_hinge.c,
               joint_ik.c, joint_limit_position.c,
               joint_limit_rotation.c, joint_lock.c, joint_motor.c,
               joint_twist.c

  game_state/: game_state_core.c, game_state_query.c

  jobs/: phys_batch_size.c, phys_job_dispatch.c

  memory/: phys_arena.c, phys_arena_query.c, phys_pool_buffers.c,
           phys_pool.c, phys_pool_net.c, phys_pool_query.c

  muscle/: activation.c, force_curve.c, geometry.c,
           muscle_pair.c, muscle_unit.c, tendon.c

  net/: prediction.c, snapshot_decode.c, snapshot_encode.c,
        snapshot_quantize.c

  query/: closest_point.c, overlap.c, raycast.c

  solver/: constraint_build.c, constraint_color.c,
           constraint_mass.c, constraint_rebuild.c,
           constraint_tangent.c, island.c, island_uf.c,
           joint_angular_projection.c, solver_transition.c,
           solver_transition_apply.c, tgs_solve.c, xpbd_solve.c
           position_projection/: ldlt_solve.c, position_projection.c,
                                 velocity_sync.c
           cg/: cg_alloc.c, cg_apply.c, cg_assemble.c, cg_solve.c

  stages/: aabb_update.c, amortized_interp.c, broadphase.c,
           cache_commit.c, ccd.c, ccd_statics.c,
           constraint_build_stage.c, halo_closure.c, integrate.c,
           island_build.c, island_tier_promote.c, manifold_build.c,
           narrowphase.c, occlusion_nudge.c, spatial_update.c,
           stabilization.c, step_plan.c, tier_classify.c,
           tier_stabilization.c
           par/: broadphase_par.c, collision_fused_par.c,
                 constraint_build_par.c, integrate_par.c,
                 manifold_build_par.c, narrowphase_par.c,
                 spatial_update_par.c, stabilization_par.c,
                 tgs_solve_par.c, tier_classify_par.c,
                 xpbd_solve_par.c

  tier/: tier_cross.c, tier_list.c, tier_list_query.c, tier_params.c

  world/: phys_cmd_drain.c, phys_cmd_push.c, phys_tick_runner.c,
          phys_tick_runner_pause.c, tick_parallel.c, world_body.c,
          world_collider.c, world_collider_point.c, world_compound.c,
          world_config.c, world_exclude_pair.c, world_impact.c,
          world_impact_config.c, world_joint.c, world_lifecycle.c,
          world_query.c, world_static_bvh.c

  animated/: phys_anim_entity_create.c, phys_anim_entity_drive.c,
             phys_anim_entity_sync.c
