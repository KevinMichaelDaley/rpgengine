---
id: rust-rpg-5hj
status: open
deps: [rust-rpg-aqq, rust-rpg-8eb, rust-rpg-y97]
links: []
created: 2026-01-17T18:02:49.532234365-08:00
type: epic
priority: 2
---
# P_006 — Physics System (Rigid Bodies, Collisions, Force Fields, Articulated Bodies)

## P_006 — Physics System (Rigid Bodies, Collisions, Force Fields, Articulated Bodies)

### Design Intent
Deliver a deterministic physics stack with fixed timestep that includes rigid bodies (linear + angular dynamics), broadphase and narrowphase collision detection, physically plausible collision response (restitution + friction), force fields/sensors, and articulated bodies via constraints (hinge, ball-and-socket, distance). Emphasis is on stability, determinism, and performance suitable for large crowds.

### Specification
#### Timestep & Integration
- Fixed timestep (e.g., 60 Hz). Accumulate frame time and step in discrete ticks.
- Integrator: semi-implicit (symplectic) Euler for linear and angular components.
  - Linear: `v += inv_mass * (F * dt)`; `x += v * dt`
  - Angular: `ω += I_inv * (τ * dt)`; `q = normalize(q + 0.5 * dt * (ω ⊗ q))`
- Sleeping/activation: bodies go to sleep after low-velocity threshold for N ticks; wake on contact/force.

#### Rigid Body
- `rigid_body_t` (public):
  - `vec3_t position`, `quat_t orientation`
  - `vec3_t linear_velocity`, `vec3_t angular_velocity`
  - `float mass`, `float inv_mass`
  - `mat3_t inertia_body`, `mat3_t inertia_inv_world`
  - `vec3_t force_accum`, `vec3_t torque_accum`
  - `bool dynamic` (static if false), `bool sleeping`
- Ownership: bodies managed by a pool; IDs/handles used externally.

#### Colliders & AABBs
- `collider_t` (public): tagged union of primitive shapes:
  - `sphere { radius }`
  - `box { half_extents }`
  - `capsule { half_height, radius }`
- `aabb_t { vec3_t min, max }` computed per body/collider in world space.

#### Broadphase
- `spatial_hash_t { float cell_size; buckets }` with integer 3D keys.
- Insert bodies’ AABBs into overlapping buckets; queries return deduplicated candidate pairs.

#### Narrowphase & Contacts
- Contact generation for primitive pairs: sphere–sphere, sphere–box, box–box (SAT-lite or GJK-lite optional), capsule variants.
- Contact manifold: up to 4 contact points with normal, penetration depth, and local/world anchors.

#### Collision Response (Sequential Impulse)
- Pairwise solver with restitution and Coulomb friction.
- Warm starting optional; bias term for penetration correction (baumgarte or split impulse).
- Iterative solver (e.g., 10–20 iterations) over contact constraints.

#### Constraints & Articulated Bodies
- Joints (public types capped to meet header type rule via separate modules if needed):
  - Distance (ball) constraint
  - Ball-and-socket
  - Hinge (one rotational DOF) with limits + optional motor target
- Constraint solver unified with contact solver; constraints expressed as velocity-level equations.

#### Force Fields & Sensors
- Global gravity field (configurable).
- Local fields: directional wind, radial attraction/repulsion with falloff.
- Sensors: volumes that produce overlap sets (no response) to feed gameplay.

#### Ray Tests
- Ray vs AABB and Ray vs primitive colliders (sphere, box) using slab/analytic methods.

### Implementation Steps
1. Fixed timestep accumulator and stepping loop.
2. Rigid body pool: allocation, initialization, integration.
3. Colliders + world AABB computation.
4. Broadphase spatial hash insertion/query with dedup.
5. Narrowphase contact generation for supported primitive pairs.
6. Sequential impulse solver: restitution, friction, penetration correction.
7. Constraints: implement distance, ball-and-socket, hinge (limits + optional motor).
8. Sleeping/activation logic.
9. Force fields application and sensors (overlap queries without response).
10. Ray intersections for AABB and primitives.
11. Instrumentation hooks (iterations, contacts per frame) for debug tests.

### Architectural Considerations
- Deterministic stepping: physics runs in fixed ticks; renderer interpolates.
- Avoid per-step allocations: use frame arenas or pre-allocated buffers.
- Stable ordering optional; document if solver requires pair ordering.
- Clear ownership: no hidden global mutable state except configuration.
- Clean under `-Wall -Wextra -Wpedantic`; pure C11.

### Unit Tests (RED-first)
**Happy Path**
1. **Rigid body free fall**
   - gravity-only: position after N ticks matches `x = x0 + v0*t + 0.5*g*t^2` within epsilon.
2. **Resting contact on plane**
   - box on static ground: after solver, body stays at rest (no sinking), normal force balances gravity.
3. **Elastic bounce (restitution)**
   - sphere drops onto ground with restitution r; rebound velocity ~ r * incident.
4. **Frictional slide-to-stop**
   - box with initial horizontal velocity on ground: velocity decays and box stops; static vs kinetic friction rules applied.
5. **Broadphase finds neighbors**
   - two bodies in same cell: query returns pair once (dedup).
6. **Narrowphase contact generation**
   - sphere–box overlap produces contact normal and penetration depth consistent with shapes.
7. **Constraints: distance joint length maintained**
   - two dynamic bodies linked by distance joint preserve distance under gravity.
8. **Constraints: hinge rotation**
   - hinge allows rotation around axis but blocks other DOFs; angular velocities match expected.

**Edge Cases**
9. **AABB boundary overlap**
   - AABB exactly on cell boundary appears in both cells.
10. **Raycast miss/hit**
   - ray passes near collider but not intersect → false; direct hit returns correct `t`.
11. **Sleeping/activation thresholds**
   - body with tiny velocity goes to sleep; applying force wakes it reliably.
12. **Stack stability (small pile)**
   - two boxes stacked: solver resolves contacts without jitter; stable after a few ticks.
13. **Constraint limits**
   - hinge angle limit prevents rotation beyond bound; motor target tracks setpoint within tolerance.

**Failure Modes**
14. **Invalid collider parameters**
   - negative radii/half-extents rejected with explicit error.
15. **Invalid mass/inertia**
   - zero or negative mass for dynamic body rejected (or treated as static) with explicit behavior.
16. **Broadphase with invalid cell size**
   - `cell_size <= 0` rejected.
17. **Narrowphase degeneracy**
   - coincident centers (sphere radius 0) returns explicit failure.

### Regression Tests (RED-first)
1. **No energy explosion**
   - under repeated contact resolution, kinetic energy stays bounded; no NaNs.
2. **Bias term penetration fix**
   - resting contact penetration stays below small epsilon across steps.
3. **Friction directions regression**
   - tangential friction axes computed consistently; no axis flip with tiny normals.
4. **Constraint drift control**
   - distance joint drift remains under threshold over long runs.
5. **Ray slab sign correctness**
   - negative ray directions compute correct entry/exit times.

### Cumulative Integration Tests (RED-first, cumulative through P_006)
1. **Rigid bodies in ECS (P_000..P_006)**
   - create ECS entities with rigid bodies + colliders; update via job system with per-worker frame arenas.
   - verify deterministic positions/velocities, contact counts, and no deadlocks.
2. **Scene: stack + hinge door**
   - build a small scene with a stack of boxes and a hinged “door”; run N ticks and assert stable rest + hinge behavior.
3. **Force field wind + sensors**
   - apply directional wind; verify movement; sensors report overlaps deterministically.

---



