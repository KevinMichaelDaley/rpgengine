# C11 Fiber Game Engine — Fully Expanded Prompts (P_000–P_020)
**Scope:** Pure C11, strict TDD-first, extreme modularity, fiber-based job system as the foundation.

This document expands each prompt into:
- **Design Intent**
- **Specification**
- **Implementation Steps**
- **Architectural Considerations**
- **Unit Tests (RED-first requirements)**
- **Regression Tests (RED-first requirements)**
- **Cumulative Integration Tests (RED-first requirements)**

---

## Networking Aggregator — Schemas & Channel Policies

This section standardizes networking schemas and channel policies used across prompts `P_007`, `P_008`, and `P_009` (and animation references in `P_004`/`P_012`, plus AI references in `P_013`) to ensure consistency and reduce ambiguity.

### Design Intent
Provide a single reference for component schema IDs, quantization rules, and channel usage to avoid mismatches across modules.

### Specification
- **Schema Registry:**
   - Assign stable IDs for replicated components: `transform`, `rigid_body_correction`, `inventory_container`, `quest_log`, `dialogue_state`, `animation_state`, `ai_state`, `custom_attributes`.
   - `animation_state` fields: `clip_id` (varint), `time` (quantized float), `blend_weight` (quantized), optional `parameters` (varint IDs + quantized floats).
   - `ai_state` fields: `behavior_id` (varint), `goal_id` (varint), `plan_version` (varint), optional `path_seq` (varint), minimal timers (quantized floats) if needed.
   - `custom_attributes` fields: varint `module_id`, varint `attr_id`, `type_tag` (u8), value encoded per type (varint/sfixed/float/bytes); supports create/update/delete ops.
   - Each schema documents bit-packing layout, endian, and quantization (for floats).
- **Channel Policies:**
   - Unreliable high-rate: `transform`, minor physics corrections, `animation_state` time updates, transient `ai_state` timers.
   - Reliable ordered: `entity_create/destroy`, `inventory_tx/delta`, `quest_event/delta`, `dialogue_updates`, `snapshot_chunks`, `animation_clip_change`, `ai_state` behavior/goal transitions, `custom_attributes` create/update/delete.

- **Security & Versioning:**
  - Modules must register `module_id` and attribute schemas via a reliable manifest before sending `custom_attributes` updates.
  - Unknown `module_id/attr_id` updates are dropped; version mismatches trigger manifest re-request.
- **Quantization & Epsilon:**
   - Positions: 1–2cm quantization; rotations: 10–12 bits per axis or quaternion; define epsilon comparisons for tests.
- **Baseline/Delta Rules:**
   - Baselines per client; delta encodes only dirty fields; fallback to reliable snapshots on baseline loss.
- **Interest Management:**
   - Spatial budgeting with per-tick byte caps; nearest and visible entities prioritized; dirty-bit tracking prevents redundant sends.

### Regression Guardrails
- Golden bytes for schemas; ack/bitfield mapping locked by tests.
- Deterministic quantization/dequantization across runs.


> **Global constraints (apply to every prompt)**
> - **TDD mandatory:** write tests first; implementation only after failing tests exist.
> - **Pure C11:** `-std=c11`, no compiler extensions.
> - **Warnings:** clean under `-Wall -Wextra -Wpedantic`.
> - **Extreme modularity:**
>   - **2-Type Rule:** a public header exposes ≤ 2 public types.
>   - **4-Function Rule:** a `.c` file has ≤ 4 non-static functions (static helpers allowed).
> - **No third-party libs** unless explicitly whitelisted.

---

## P_000 — Fiber Runtime & Job System (FOUNDATION)

### Design Intent
Provide a deterministic, low-overhead execution model that scales across CPU cores while avoiding OS-thread oversubscription and lock contention. Fibers are used for cheap cooperative context switches; a job system schedules work across a small, fixed number of OS threads.

### Specification
#### Key Concepts
- **OS worker threads (N):** fixed at startup.
- **Fibers (M):** stackful coroutines, multiplexed onto threads.
- **Jobs:** units of work; may spawn other jobs; may wait on counters/events.

#### Public API (example shape; tests define final API)
- Fiber:
  - `fiber_t* fiber_create(fiber_fn fn, void* user, fiber_stack_t stack);`
  - `void fiber_yield(void);`
  - `void fiber_resume(fiber_t* fiber);` *(scheduler-internal)*
- Job system:
  - `job_id_t job_dispatch(job_fn fn, void* user, job_priority_t p);`
  - `void job_wait(job_counter_t* counter);`
  - `void job_counter_init(job_counter_t* c, uint32_t initial);`
  - `void job_counter_dec(job_counter_t* c);` *(called by job completion path)*

#### Behavioral Requirements
- **Cooperative only:** no preemption; yielding occurs via explicit calls or waits.
- **Wait without blocking:** `job_wait` must park the current fiber and schedule another job/fiber on the same OS thread.
- **Work stealing:** idle threads steal from other thread queues.
- **No allocations in hot scheduling path:** queues and fiber stacks allocated upfront (arena/pool).
- **Deterministic shutdown:** all workers join; all fibers released back to pools.

### Implementation Steps
1. **Platform abstraction:** threads, TLS, atomics, time.
2. **Context switching:** choose one mechanism and lock it down:
   - `setjmp/longjmp`-based context (portable-ish but tricky for stacks), or
   - minimal per-platform assembly (recommended for correctness, but platform-specific).
3. **Fiber stack management:** fixed-size stacks from a stack-pool.
4. **Per-thread scheduler loop:** pop job → run job on a fiber → handle yields/waits.
5. **Work stealing queues:** bounded ring buffers (MPMC or per-thread SPSC + stealing).
6. **Wait primitives:** counters/events that park fibers and requeue them on signal.
7. **Instrumentation hooks:** counters for steals, queue depth, yields (for debug builds).

### Architectural Considerations
- **No blocking syscalls inside fibers.** If unavoidable, wrap in an async job that blocks an OS thread intentionally (rare).
- **Reentrancy:** job functions must not depend on thread-local global mutable state.
- **Starvation avoidance:** add basic fairness (e.g., steal threshold, round-robin).
- **Debuggability:** provide a compile-time option to run single-threaded for deterministic tests.

### Unit Tests (RED-first)
**Happy Path**
1. **Fiber yield/resume ordering**
   - A fiber increments a shared int, yields, increments again.
   - Scheduler runs: verify increments happen in expected interleaving.
2. **Fan-out / fan-in via counter**
   - Dispatch 1 parent job that spawns 100 child jobs; parent waits on counter.
   - Verify parent completes and all children ran exactly once.
3. **Wait parks fiber, not the worker**
   - Two jobs on the same worker: A waits on a counter/event; B continues running.
   - Verify B completes while A is parked; later signal/unpark A.
4. **Work stealing makes progress**
   - Pin jobs to one worker queue; start multiple workers; verify other workers steal and execute.
5. **Priority scheduling respects ordering (if priorities exist)**
   - Enqueue a mix of priorities; verify higher priority runs first (or document that priorities are hints).
6. **Deterministic single-thread mode**
   - Run identical job graphs twice with a fixed seed; verify identical execution trace.

**Edge Cases**
7. **Wait on already-satisfied counter returns immediately**
   - Initialize counter to 0; call `job_wait`; verify no park/yield occurs.
8. **Counter underflow protection**
   - Decrement counter at 0 must return an error (or be a no-op) and must not wrap.
9. **Fiber stack boundary safety**
   - Deep recursion / large stack frame should be detected in debug (or documented as undefined) but must not corrupt other fibers.
10. **Queue wrap-around**
   - Push/pop enough jobs to wrap ring indices; verify ordering and no loss/duplication.
11. **Scheduler shutdown drains safely**
   - Request shutdown while queues are non-empty; verify all worker threads join and no jobs run after shutdown returns.

**Failure Modes**
12. **Invalid dispatch arguments**
   - `job_dispatch(NULL, ...)` must return an explicit error and not crash.
13. **Pool exhaustion behavior**
   - Exhaust fiber pool or queue capacity; `job_dispatch` fails explicitly and system stays usable.
14. **Double-wait / double-signal robustness**
   - Waiting twice on the same event/counter (or signaling twice) must not deadlock or resume a completed fiber.

### Regression Tests (RED-first)
1. **No double-execution after steal**
   - A job stolen across workers must execute exactly once.
2. **No lost wakeups**
   - Signal a counter/event concurrently with a fiber parking; fiber must eventually resume.
3. **No counter wraparound**
   - Repeated inc/dec cycles must never produce a huge value from underflow.
4. **No resume-after-complete**
   - Completed fiber/job must not be re-queued or resumed.
5. **Deterministic trace in debug**
   - A fixed schedule mode (single-thread or deterministic steal policy) yields stable traces.

### Cumulative Integration Tests (RED-first, cumulative through P_000)
1. **Mini-frame execution**
   - Simulate 120 “frames”: each frame dispatches a fan-out graph + a parked fiber that resumes later.
   - Verify no deadlocks, all counters reach 0, and shutdown is clean.
2. **Instrumentation invariants**
   - If instrumentation counters exist: `jobs_started == jobs_completed`, and `fibers_in_pool` returns to baseline after shutdown.

---

## P_001 — Core Math Library (OpenGL-friendly)

### Design Intent
Provide a minimal but robust math foundation with GPU-native layouts (column-major matrices), explicit C APIs (no operator overloading), safe normalization, and quaternion support for stable rotation/interpolation.

### Specification
#### Public Types (split across headers to satisfy 2-Type Rule)
- `vec2_t`, `vec3_t`, `vec4_t`
- `quat_t`
- `mat4_t` (column-major `[16]`)

#### Required Operations
- Vectors:
  - `add/sub/scale`, `dot`, `magnitude`, `normalize_safe(epsilon)`, `lerp`
  - `cross` for `vec3_t`
- Matrices:
  - constructors: identity, translation, scaling, rotation_x/y/z
  - camera: look_at, perspective, ortho
  - ops: mul_mat4, mul_vec4, transpose, inverse (general or affine-fastpath)
- Quaternions:
  - from_axis_angle, normalize_safe, conjugate, mul_quat, slerp
  - to_mat4

#### Numeric Rules
- Use `float` everywhere.
- `normalize_safe`: if length < epsilon, return zero vector (or identity quat for quats).

### Implementation Steps
1. Define POD structs and storage.
2. Implement scalar ops and dot/cross.
3. Normalize + lerp with epsilon.
4. Quaternion math + slerp.
5. Matrix constructors + multiplication.
6. Implement `perspective` matching OpenGL clip space (document conventions).
7. Inverse: start with affine inverse (TRS) then add general inverse if needed.

### Architectural Considerations
- **No heap allocation.**
- **No hidden global constants** beyond explicit `const`.
- **Layout guarantees:** document and static-assert sizes where possible.
- **Determinism:** tests should tolerate tiny float error; use epsilon comparisons.

### Unit Tests (RED-first)
**Happy Path**
1. **Translation mat4 × vec4**
   - `T = translation(10,0,0)`
   - `p=(0,0,0,1)` → result `(10,0,0,1)`
2. **Identity invariants**
   - `I * v == v`, `I * I == I`, `transpose(I) == I`.
3. **Vector dot and magnitude**
   - `dot(v,v)` equals `magnitude(v)^2` within epsilon.
4. **Normalize_safe (non-zero)**
   - Normalizing `(3,4,0)` yields approx `(0.6,0.8,0)`; magnitude ~1.
5. **Quaternion to_mat4 preserves axis-angle**
   - `quat_from_axis_angle(Z, 90°)` rotates `(1,0,0)` to `(0,1,0)` within epsilon.
6. **Matrix multiply associativity (tolerant)**
   - `(A*B)*C` approximately equals `A*(B*C)` for a small set of deterministic matrices.

**Edge Cases**
7. **Normalize_safe zero vector**
   - `normalize_safe((0,0,0), eps)` returns `(0,0,0)`.
8. **Normalize_safe epsilon corner cases**
   - `eps == 0` must not divide by zero; function returns a finite vector (document the chosen behavior).
9. **Cross product basis**
   - `X×Y = Z`, `Y×X = -Z`, and `X×X = 0`.
10. **Look-at degeneracy handling**
   - When `eye == target` (or up parallel to view), input must return an explicit failure (or a documented fallback) without NaNs.

**Failure Modes**
11. **Inverse failure on singular matrix**
   - Inverting a singular matrix returns explicit failure (or produces a documented identity fallback) and never produces NaNs.
12. **Perspective parameter validation**
   - `near <= 0`, `far <= near`, `aspect <= 0`, or `fov <= 0` must return explicit failure.

### Regression Tests (RED-first)
1. **Perspective regression (index-level)**
   - fov 90°, aspect 1, near 0.1, far 100
   - assert specific indices are within epsilon of expected values.
2. **Quaternion slerp endpoints and normalization**
   - `t=0` returns source; `t=1` returns target; intermediate stays ~unit length.
3. **Handedness / convention lock-in**
   - `look_at` + `perspective` combination places a known point in expected clip-space (within epsilon).

### Cumulative Integration Tests (RED-first, cumulative through P_001)
1. **Math under scheduler load (P_000 + P_001)**
   - Dispatch many jobs that compute deterministic matrix/quaternion results into an output array.
   - Verify results match a single-thread reference run.

---

## P_002 — Memory Systems (Arena + Pool)

### Design Intent
Eliminate unpredictable allocator behavior in the hot loop by using arena allocation for frame-temporary data and a typed pool for long-lived objects/components.

### Specification
#### Arena
- Backed by a byte buffer.
- `arena_alloc(alignment, size)` returns aligned pointer or NULL.
- `arena_reset()` resets offset to 0.
- Optional: `arena_mark/arena_pop_to_mark` for nested lifetimes.

#### Pool
- Fixed-type pool storing `T` in contiguous memory with free-list.
- Handles use `(index, generation)` or `(index)` + external gen array.
- `pool_alloc` returns handle; `pool_free(handle)` returns slot to free-list.
- `pool_get(handle)` validates generation.

### Implementation Steps
1. Arena struct with buffer, capacity, offset.
2. Alignment padding.
3. Pool arrays: dense storage + free list + generations.
4. Handle struct and validation helpers.

### Architectural Considerations
- Arena pointers become invalid after reset (document).
- Pool must avoid ABA bugs via generation.
- No per-alloc mallocs inside pool operations.

### Unit Tests (RED-first)
**Happy Path**
1. **Arena reuse address stability**
   - allocate 1000 ints; record first ptr
   - reset; allocate again; first ptr matches
2. **Arena alignment (common)**
   - alloc u8 then u64; u64 pointer aligned to 8
3. **Arena sequential allocations do not overlap**
   - allocate N fixed-size blocks; verify each range is disjoint and within buffer.
4. **Pool alloc/free reuse**
   - allocate handles A,B; free A; allocate C; C reuses A’s slot.
5. **Pool get returns stable pointer while alive**
   - `pool_get(handle)` returns same address until freed.

**Edge Cases**
6. **Arena out-of-memory returns NULL**
   - request `size > remaining` returns NULL and does not advance offset.
7. **Arena supports large alignment**
   - allocate with alignment 16/32/64; pointer must be aligned.
8. **Zero-size allocation semantics**
   - `arena_alloc(alignment, 0)` must return a consistent pointer or NULL (document), and must not corrupt state.
9. **Mark/pop nested lifetimes (if supported)**
   - take two marks; allocate; pop to inner mark; verify offset restored; pop to outer mark; restored again.
10. **Pool capacity boundary**
   - allocate exactly capacity; next alloc returns explicit failure.

**Failure Modes**
11. **Pool generation mismatch**
   - alloc handle A; free; alloc reuses index with incremented generation; old handle must fail validation.
12. **Invalid free**
   - freeing an invalid handle (bad index/generation) returns error and does not corrupt freelist.
13. **Double free**
   - freeing the same valid handle twice returns error and does not corrupt freelist.

### Regression Tests (RED-first)
1. **Alignment padding off-by-one**
   - sequences of mixed-size allocations must keep every returned pointer aligned.
2. **Freelist corruption guard**
   - randomized alloc/free sequences must not produce cycles or duplicates in freelist.
3. **Generation wrap behavior**
   - if generation is small (e.g., u16), define and test wrap semantics (error, saturate, or allow wrap with global epoch).

### Cumulative Integration Tests (RED-first, cumulative through P_002)
1. **Arena usage inside jobs (P_000 + P_001 + P_002)**
   - each job allocates scratch from a per-worker arena, computes math results, and resets arena at “frame end”.
   - verify no cross-job aliasing and outputs match reference.

---

## P_003 — ECS Core (Sparse Set)

### Design Intent
Provide cache-friendly component storage with O(1) insertion/removal and stable entity identity using generation counters.

### Specification
#### Entity
- `entity_t { uint32_t index; uint32_t generation; }`

#### SparseSet<T>
- `T* dense`
- `entity_t* dense_entities`
- `uint32_t* sparse` (entity_index → dense_index or UINT32_MAX)
- swap-remove on deletion

#### World
- Manages entity allocation/free lists and per-component stores.
- C11 has no templates; use one of:
  - **Macro-generated sparse sets** per component type, or
  - **Type-erased store** with size/stride + function pointers (more complex)
- For this project: **macro-generated** stores for performance and simplicity.

### Implementation Steps
1. Entity allocator with free list + generation array.
2. Sparse set insert/get/remove.
3. Macro to define `sparse_set_<name>` per component.
4. World registry for known component sets.

### Architectural Considerations
- Keep components POD; avoid pointers where possible.
- Swap-remove must update moved entity’s sparse entry.
- Queries return dense arrays for tight iteration.

### Unit Tests (RED-first)
**Happy Path**
1. **Entity create/destroy lifecycle**
   - create N entities; destroy a subset; create again; verify reuse with generation bump.
2. **Sparse-set insert/get/remove basic**
   - insert component for entity E; `get(E)` returns pointer; remove; `get(E)` returns NULL.
3. **Swap-removal integrity**
   - Insert A,B,C; remove B
   - Verify dense packed and C moved; sparse[C] updated.
4. **Remove last element**
   - remove the last dense entry; verify no swap needed and sparse cleared.
5. **Iterate dense arrays**
   - iteration visits all live components exactly once.

**Edge Cases**
6. **Duplicate insert**
   - inserting a component twice for the same entity must return explicit error or replace deterministically (document).
7. **Remove non-existent**
   - removing a component that isn’t present returns explicit “not found” and makes no changes.
8. **Sparse sentinel correctness**
   - sparse entries for missing components are `UINT32_MAX` (or chosen sentinel) and remain stable.
9. **Capacity growth behavior (if resizable)**
   - growth preserves all mappings; dense order either preserved or documented.

**Failure Modes**
10. **Generation mismatch access**
   - Create entity E; destroy; create new entity reusing index with new generation
   - Old handle must not access components.
11. **Invalid entity index**
   - `get/remove` for out-of-range entity index returns error and does not crash.
12. **World entity exhaustion**
   - creating beyond max entity capacity returns error.

### Regression Tests (RED-first)
1. **Sparse update on swap**
   - after removal swaps last element into hole, moved entity’s sparse index must update.
2. **Stale pointers invalidation**
   - pointers returned by `get` must not remain valid after remove/swap (document), and tests should ensure users don’t rely on them.
3. **Free-list reuse without generation bump bug**
   - ensure every reuse increments generation exactly once.

### Cumulative Integration Tests (RED-first, cumulative through P_003)
1. **ECS system loop determinism (P_000..P_003)**
   - dispatch jobs that run a deterministic “system” over dense component arrays (single-threaded or explicitly synchronized).
   - verify entity/component counts and computed math outputs are stable across runs.

---

## P_004 — Renderer Core (OpenGL Wrappers)

### Design Intent
Wrap unsafe OpenGL resource lifetimes into explicit, minimal C APIs with clear ownership, enabling predictable cleanup and testability.

### Specification
- GL loader: function pointer table.
- `shader_program_t`:
  - compile vertex+fragment, link, bind
  - uniform setters: mat4, vec3, int, float
- `vbo_t`, `vao_t`:
  - create/destroy
  - upload data (size + pointer)
  - attribute layout binding

#### Skeletal Animation & GPU Skinning
#### Render Pipeline Graph (Stages & Passes)
- Define a minimal render pipeline graph: nodes for passes (skybox, depth pre-pass optional, main forward pass, post chain entry) with explicit dependencies.
- Stage interfaces: `begin_pass`, resource bindings, draw submission, `end_pass`.
- Resource views: define attachments (color/depth) and transient buffers; avoid hard-coding effects.
- This enables implementing advanced effects elsewhere without changing core wrappers.
- Bone palette buffers: UBO/SSBO/TBO depending on capability.
- Skinning shader: vertex shader applies weighted bone transforms to mesh vertices.
- ECS integration: per-entity `skeleton`/`skin` components refer to bone transforms.
- Job system: CPU-side evaluation jobs compute bone matrices from animation clips and write to per-entity palettes.

### Implementation Steps
8. Define render pipeline graph structs and pass interfaces; implement pass execution order.
9. Provide default pipeline: skybox → forward main → post chain stub.
1. Define GL loader interface (platform provides proc addresses).
2. Implement shader compilation with error logs.
3. Implement program linking and uniform lookup caching.
4. Implement buffers and vertex array setup.
5. Implement skinning shader program(s); define attribute layouts for weights/indices.
6. Implement bone palette buffer creation/update (UBO/SSBO/TBO) and binding per draw.
7. Integrate ECS/job pipeline to evaluate animation clips, produce bone matrices, and stage GPU uploads.

### Architectural Considerations
- Pass ordering deterministic; graph avoids global mutable state.
- Keep pipeline generic; no specific post effects here—only plumbing.
- No global mutable GL state in wrappers; require explicit bind.
- Error handling: return status + log buffer.
- Unit tests use a mocked GL table (function pointers replaced by test stubs).
- Skinning must avoid per-frame mallocs; prefer persistent buffers.
- Capability gating for buffer types; fallback path documented.
- Deterministic CPU evaluation via jobs; GPU upload ordering stable in tests.

### Unit Tests (RED-first)
**Happy Path**
1. **Shader compile + link success path**
   - mocked GL returns success; verify program handle stored and bind uses correct GL calls.
2. **Uniform upload calls correct GL functions**
   - verify `glUniformMatrix4fv` called with expected params.
3. **VBO/VAO create/destroy pairs**
   - create then destroy must call GL delete exactly once per resource.
4. **Attribute layout binding**
   - verify attribute pointer/format calls receive correct strides/offsets.
5. **Bone palette upload/bind**
   - mocked GL receives buffer update calls with correct sizes; binding per draw uses expected binding point/index.
6. **Skinning shader uniform setup**
   - verify bone palette bound and matrices uploaded prior to draw.

**Edge Cases**
5. **Uniform location caching**
   - first set queries location; subsequent sets reuse cached location.
6. **Zero-size uploads**
   - uploading 0 bytes must be a no-op or explicit error (document), but must not crash.
7. **Double destroy is safe**
   - destroying an already-destroyed wrapper is a no-op or explicit error; must not call GL delete twice.
8. **Palette size limits**
   - exceeding max bones per draw returns explicit error or splits draw; document behavior.
9. **Fallback buffer path**
   - when SSBO unsupported, UBO/TBO path engages; tests verify capability gating.

**Failure Modes**
8. **Shader compile failure path**
   - mock compile returns failure; verify error returned and log captured.
9. **Program link failure path**
   - compile succeeds but link fails; verify error returned and log captured.
10. **Missing GL function pointers**
   - if required GL entry points are NULL, wrapper init/create must return explicit failure.
11. **Skinning shader link failure**
   - skinning program link fails; error captured and renderer falls back or reports failure explicitly.

### Regression Tests (RED-first)
1. **Error log truncation**
   - very long compiler log must be safely truncated and NUL-terminated.
2. **Uniform type mismatch guard**
   - if wrapper tracks uniform types, setting wrong type returns error (otherwise document no checking).
3. **No implicit global binds**
   - wrapper must not assume any prior GL state; tests start from “unknown state” and verify required binds happen.
4. **Bone palette index mapping stability**
   - mapping from bone indices to buffer offsets remains stable across runs.
5. **Attribute semantics lock-in**
   - vertex attribute locations/types for weights/indices documented and tested.

### Cumulative Integration Tests (RED-first, cumulative through P_004)
1. **Render command generation loop (P_000..P_004)**
   - create a small ECS world with transform components; build view/projection matrices; dispatch jobs that generate mocked draw calls.
   - verify deterministic call sequence against the mocked GL table.
2. **GPU skinning path (P_000..P_004)**
   - create an entity with skeleton/skin; job evaluates bone matrices; renderer uploads palette and issues draw; verify correct GL call sequence and buffer bindings.
3. **Pipeline execution ordering**
   - execute default pipeline (skybox → forward → post stub); mocked GL verifies pass begin/end order and resource bindings.

---

## P_005 — Geometry Clipmaps (Terrain)

### Design Intent
Render large terrain with constant geometry memory by using nested grids whose offsets “snap” in a toroidal fashion around the camera, pushing complexity to shaders.

### Specification
- One shared `MxM` grid mesh.
- `clipmap_layer_t { float scale; vec2_t offset; }`
- Offset snapping:
  - `offset = floor(camera_pos / (base_spacing * scale)) * (base_spacing * scale)`
- Shader uses scale+offset to compute world pos; samples heightmap.

### Implementation Steps
1. Grid mesh generation (positions + indices).
2. Layer definitions and update logic.
3. Uniform upload per layer.
4. Blending factor computation (distance-based).

### Architectural Considerations
- CPU only updates offsets; geometry static.
- Avoid per-frame allocations; layer arrays fixed.
- Tests focus on CPU offset math (shader tested by invariants).

### Unit Tests (RED-first)
**Happy Path**
1. **Grid mesh generation counts**
   - for `M`, verify vertex count is `M*M` and index count matches the chosen topology.
2. **Offset snapping per LOD**
   - simulate camera moves; layer0 snaps at `base_spacing`; layer1 snaps at `base_spacing*2`.
3. **Layer uniform packing**
   - verify `scale` and `offset` upload values match CPU state exactly.

**Edge Cases**
4. **Negative coordinates snapping**
   - camera at negative positions must snap consistently (no oscillation around 0).
5. **Large world coordinates**
   - camera far from origin (large magnitude) must not overflow integer snapping and must remain stable.
6. **Texture coordinate wrapping**
   - ensure computed UV wraps into [0,1) for both positive and negative offsets.
7. **Blend factor monotonicity**
   - blending factor increases/decreases monotonically with distance (as defined) and stays clamped.

**Failure Modes**
8. **Invalid parameters**
   - `M < 2`, `base_spacing <= 0`, or `scale <= 0` must return explicit failure.

### Regression Tests (RED-first)
1. **No shimmer at snap boundaries**
   - moving camera by tiny deltas across a snap boundary must not cause offset to oscillate between two values.
2. **UV wrap for negative values**
   - ensure wrap logic does not produce negative UVs due to C `fmodf` semantics.

### Cumulative Integration Tests (RED-first, cumulative through P_005)
1. **Terrain layer update in jobs (P_000..P_005)**
   - dispatch a job to update clipmap offsets and a job to upload uniforms via mocked GL.
   - verify layer offsets/uniforms are consistent and deterministic per “frame”.

---

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

## P_007 — Networking & Replication (RUDP, Snapshot/Delta, Prediction)

### Design Intent
Enable full entity and component state synchronization from a server-authoritative simulation using UDP with reliability channels, snapshot/delta replication, client-side prediction with reconciliation, time synchronization, and interest management. Maintain low latency for high-rate state (movement), while guaranteeing ordered delivery for critical gameplay (inventory, dialogue, quests).

### Specification
#### Transport & Channels
- Packet header: `protocol_id: u32`, `sequence: u16`, `ack: u16`, `ack_bits: u32` (unchanged).
- Channels:
   - Unreliable high-rate (movement/transform deltas, predicted state corrections).
   - Reliable ordered (inventory, quest/dialogue, entity create/destroy, chunked snapshots).
   - Optional priorities (document as hints if not strictly enforced).

#### Time Synchronization
- Client estimates server clock offset via periodic pings; offset = median half-RTT.
- Jitter buffer for interpolation windows; clamp drift to avoid visual pops.

#### Entity & Component Replication
- Server authoritative entities use `entity_t { index, generation }` IDs.
- Client maintains a ghost table mapping remote IDs to local handles.
- Replication messages:
   - Create: entity archetype + initial components.
   - Update: component deltas with schema IDs and bit-packed fields.
   - Destroy: invalidate generation; cleanup ghost.
- Component sync policies (examples):
   - Transform: quantized floats (pos, rot) on unreliable channel at high rate.
   - Physics corrections: unreliable corrections + reliable hard snaps on large error.
   - Inventory/quest/dialogue: reliable ordered.
   - AI state: reliable ordered behavior/goal transitions; optional unreliable timers/counters.
   - AI LOD state (optional): lower frequency, unreliable.
   - Animation state: unreliable time updates; reliable clip transitions.
   - Custom attributes (from scripting modules): reliable ordered create/update/delete after manifest registration.
   - Animation state: unreliable high-rate `time`/`blend_weight` updates; reliable ordered `clip_id` changes and animation graph transitions.

#### Snapshot/Delta Replication
- Baselines: server tracks per-client baseline snapshot ID.
- Delta: bit-packed changes from baseline (quantized, run-length or varint where helpful).
- Client ACKs snapshot IDs; server advances baseline when safe.
- Fallback: if baseline missing, send partial/full snapshot chunks reliably.

#### Interest Management
- Spatial partition (grid/hash) forms per-client interest sets.
- Budgeted bytes per tick; prioritize nearby/visible entities/components.
- Dirty-bit tracking: only changed components replicated.

#### Client-Side Prediction & Reconciliation
- Local inputs buffered with timestamps (ring buffer).
- Client predicts movement; on authoritative corrections: rewind to last confirmed state, re-simulate buffered inputs.
- Error thresholds to avoid micro-corrections; hard snaps on large divergence.

#### Security & Validation
- Validate protocol header and schema IDs; drop invalid.
- Clamp/validate client inputs on server; never trust client state.

#### Serialization Schema
- Component schemas registered with IDs; encode/decode functions bit-pack fields.
- Network byte order; avoid dynamic allocations during encode/decode.
 - Dynamic module manifest: reliable message registering `module_id` and attribute schemas (name, `attr_id`, `type_tag`, encoding). Clients persist manifest for decode.

### Implementation Steps
1. Header encode/decode + validation (protocol ID, sizes).
2. Channel abstraction (unreliable/reliable ordered) with send/recv rings.
3. Time sync pings and offset estimator.
4. Entity ghost table and ECS mapping (create/update/destroy).
5. Component schemas and bit-packed serializer for deltas (including `ai_state`, `animation_state`, and `custom_attributes`).
6. Baseline tracking and delta aggregation per client.
7. Reliable snapshot chunking and reassembly for baseline recovery (include manifests and custom attributes if needed).
8. Interest management set builder and per-tick budgeter.
9. Client prediction buffer + reconciliation logic.
10. RTT estimator and retransmit scheduling.
11. Ordered channel reassembly and delivery.
12. Instrumentation counters (bytes/tick, dropped/resent, baseline distance).

### Architectural Considerations
- Tick-driven `net_update(dt)`; no blocking recv.
- Deterministic tests via injected clock and scripted loss/jitter.
- Fixed-size buffers; avoid malloc in hot path.
- Clear ECS ownership: networking creates/updates ghost entities via world APIs.
- Quantization documented; epsilon comparisons in tests.

### Unit Tests (RED-first)
**Happy Path**
1. **Header encode/decode roundtrip**
   - pack then unpack header; fields preserved; byte order defined.
2. **ACK bitfield correctness**
   - receiving sequences updates `ack` and `ack_bits` as expected.
3. **Ordered delivery without loss**
   - receive 1,2,3 on ordered channel; deliver 1,2,3 immediately.
4. **Entity create/update/destroy replication**
   - server sends create + component init; client builds ghost; updates apply; destroy removes ghost.
5. **Delta from baseline**
   - given baseline snapshot, delta applies to reach target; client ACK advances baseline.
6. **Interest set update**
   - moving client changes interest set; replication set updates accordingly.
7. **Prediction + reconciliation**
   - client inputs predicted; authoritative correction arrives; re-simulated state matches server within epsilon.
8. **Time sync stability**
   - offset estimator converges; jitter buffer produces smooth interpolation times.
9. **Animation state replication**
   - server changes `clip_id` (reliable); client receives and switches; time updates arrive on unreliable channel; blended pose remains consistent.
10. **AI state replication**
   - server sends behavior/goal transition (reliable); client reflects state; optional timers arrive on unreliable channel; UI shows consistent state.
11. **Custom attributes replication**
   - server sends manifest; then create/update/delete attribute messages; client decodes and applies to ghost entity’s attribute table.

**Edge Cases**
9. **Sequence wraparound**
   - near `UINT16_MAX`, send/receive wrap to 0; ordering and ack logic remains correct.
10. **Duplicate packet**
   - receiving the same sequence twice is ignored and does not re-deliver.
11. **Out-of-window packet**
   - very old sequence is dropped and does not perturb ack window.
12. **Missing baseline fallback**
   - client missing baseline requests/receives snapshot chunks; state reconstructs deterministically.
13. **Partial component update**
   - only dirty fields replicated; client merges deltas correctly.
14. **Animation time wraparound**
   - clip looping wraps time; client applies wrap consistently with server.
15. **Manifest late arrival**
   - custom attribute update received before manifest is buffered or dropped; once manifest arrives, subsequent updates decode correctly.
16. **Attribute type evolution**
   - manifest version bump changes `type_tag`; client detects and requests fresh manifest; stale updates ignored.

**Failure Modes**
14. **Protocol ID mismatch**
   - wrong `protocol_id` must drop packet (no state changes).
15. **Malformed payload length**
   - too-short buffer for component schema returns explicit decode failure.
16. **Ring buffer exhaustion**
   - if send buffer is full, send returns explicit failure (or drops oldest, documented).
17. **Unknown schema ID**
   - decode rejects with error and skips update safely.
18. **Invalid animation parameters**
   - out-of-range `clip_id` or negative time rejected; state unchanged.
19. **Unknown module/attribute IDs**
   - updates for unknown `module_id/attr_id` dropped; error counters incremented.
20. **Malformed attribute payload**
   - decode failure returns error; ghost’s attribute table remains consistent.

### Regression Tests (RED-first)
1. **Resend logic**
   - drop seq 5; receive ack 6 with bit for 5 = 0; update triggers resend of 5.
2. **Ack-bits off-by-one**
   - confirm that `ack_bits` bit 0 corresponds to `ack-1` (or chosen mapping), locked by tests.
3. **RTT estimator stability**
   - repeated samples must converge; no negative RTT; variance remains bounded.
4. **Bit-packing layout lock-in**
   - schema encode/decode matches golden bytes.
5. **Quantization determinism**
   - transform quantization/dequantization remains stable across runs.
6. **Ghost leak prevention**
   - destroyed entities free ghost slots; no leaked handles across many churn cycles.
7. **Manifest schema lock-in**
   - golden bytes for manifest encode/decode; attribute tables reproduce identical state from the same stream.
8. **AI behavior/goal determinism**
   - replicated transitions produce identical client-side summaries across runs.

### Cumulative Integration Tests (RED-first, cumulative through P_007)
1. **Server→Client ECS replication (P_000..P_007)**
   - server world ticks and emits deltas; client applies to ghosts; verify component states match authoritative snapshot after reconciliation.
2. **Predicted player movement**
   - client buffers inputs, predicts, reconciles with corrections; final pose matches server within tolerance.
3. **Interest-managed bandwidth budget**
   - many entities; budget caps bytes/tick; nearest entities prioritized; verify deterministic selection and stable client state.
4. **AI + scripting attributes sync**
   - server loads a module, registers manifest, and updates custom attributes; client reflects attributes and AI state; reconciliation remains consistent.

---

## P_008 — Inventory & Modifiers (Networking & Transactions)

### Design Intent
Inventory operations are server-authoritative transactions synchronized to clients via reliable ordered messages, with deterministic modifier aggregation and conflict resolution. Clients may optimistically perform UI actions, but authoritative state arrives via deltas/snapshots with hashes to detect divergence.

### Specification
#### Components
- `item_base_t { weight, max_stack }`
- `container_t { capacity, item_entity_ids[] }`
- `modifier_t { kind(Add/Mult), stat_id, value }`

#### Transactions (Server-Authoritative)
- Operations: `ADD`, `REMOVE`, `MOVE`, `SPLIT`, `MERGE`, `EQUIP`, `UNEQUIP`.
- Each transaction carries: `tx_id`, `actor_id`, source/target container/slot, item entity handle, counts.
- Server validates capacity, stacking rules, ownership, and emits result.

#### Networking Sync
- Reliable ordered channel for transactions and inventory state deltas.
- Client keeps a pending-ops buffer for optimistic UI; reconciles on authoritative results.
- Container snapshots include a compact item list and a `state_hash` (rolling hash over `(item_id, count)` pairs).
- Deltas encode per-slot changes (add/remove/count updates) with varints.

#### Modifier Aggregation
- Deterministic rule: sum additive modifiers, then apply multiplicative modifiers per stat.
- Aggregation runs on server and client; divergence detected via `derived_hash` of aggregated results.

#### Conflict Resolution
- If client pending op conflicts with authoritative state, client rolls back local prediction and applies server delta; UI reflects updated state.

#### Security
- Server clamps counts, validates ownership; rejects malformed or out-of-capacity ops; never trusts client counts.

### Implementation Steps
1. Components and container relations.
2. Transaction executor on server with validation/errors.
3. Client pending-ops buffer + reconciliation.
4. Reliable message encode/decode for transactions and container deltas.
5. `state_hash` and `derived_hash` computation functions.
6. Modifier aggregation pipeline (deterministic order).
7. Integration with ECS and networking layer (P_007).

### Architectural Considerations
- Contiguous container storage (dense arrays); fixed capacities.
- Handles/entity IDs only; no raw pointers.
- Deterministic aggregation order; document stat combination rules.
- Server authoritative; clients reconcile.
- No per-op mallocs in hot path; use arenas or fixed buffers.

### Unit Tests (RED-first)
**Happy Path**
1. **Add/remove item success**
   - add an item entity to a container; remove it; container contents update correctly.
2. **Stacking within max_stack**
   - add two stackable items; verify stacks merge up to `max_stack`.
3. **Modifier stacking**
   - base stat + additive + multiplicative yields expected result.
4. **Deterministic aggregation**
   - multiple modifiers applied in a fixed deterministic order.
5. **Transaction roundtrip**
   - client sends `ADD`; server validates and replies; client applies authoritative delta; final state matches server.
6. **State hash matches authoritative**
   - after applying server delta, `state_hash` equals server-provided value.
7. **Derived hash matches aggregation**
   - aggregated stats produce expected `derived_hash` on client and server.

**Edge Cases**
8. **max_stack == 1**
   - stacking is disabled; adding duplicates creates separate entries or fails (document).
9. **Zero capacity container**
   - any add fails with explicit error.
10. **Remove non-existent item**
   - removing an item not in container returns explicit error.
11. **Stat overflow-adjacent**
   - large additive/multiplicative values remain finite or clamp (document) and never produce NaNs.
12. **Out-of-order transaction delivery**
   - reliable ordered ensures application in order; test buffered application.
13. **Concurrent ops on same slot**
   - server resolves deterministically; client reconciles authoritative result.

**Failure Modes**
14. **Capacity enforcement**
   - adding beyond capacity fails with explicit error.
15. **Invalid handles**
   - invalid item entity IDs/handles must not crash; return error.
16. **Malformed delta**
   - decode error for slot change returns failure; state remains consistent.
17. **Hash mismatch**
   - client detects state hash mismatch and requests snapshot.

### Regression Tests (RED-first)
1. **No double-counting on re-equip**
   - equipping/unequipping the same item repeatedly must not accumulate phantom modifiers.
2. **Add-then-mult order lock-in**
   - aggregation rule locked by tests.
3. **Transaction idempotency**
   - duplicate `tx_id` is ignored; state unchanged.
4. **Deterministic reconciliation**
   - client prediction rollback consistently produces same final state.

### Cumulative Integration Tests (RED-first, cumulative through P_008)
1. **Server→Client inventory sync (P_000..P_008)**
   - server executes transactions; client applies deltas; hashes match; containers identical.
2. **Predicted UI ops**
   - client queues pending ops and reconciles with server; final state matches authoritative.
3. **Bandwidth budget under churn**
   - heavy item churn replicated within reliable budget; client stays consistent.

---

## P_009 — Quest System (Event-driven DAG, Networked Event Log)

### Design Intent
Implement server-authoritative quest progression using a networked event log, with deterministic DAG transitions, idempotent event handling, and replay for debugging. Clients receive reliable ordered updates and can display local UI while awaiting authoritative state.

### Specification
#### Quest Definition
- Static DAG: nodes (steps) with requirements and transitions; optional rewards.
- Validation: acyclic (or explicitly mark cycles); valid references.

#### Runtime Quest Log
- Map `quest_id → {current_step, progress, flags}` with timestamp.
- Local client ghost mirrors server state; authoritative updates apply via reliable ordered channel.

#### Networked Event Log
- Server records `event { id, type, payload, timestamp }` and applies to quest log.
- Client receives event stream and/or direct quest state updates (deltas/snapshots).
- Idempotent: duplicate events are ignored on server and client.

#### Sync Policies
- Reliable ordered messages for quest state changes and events.
- Snapshots: periodic quest-log snapshots with `state_hash` allow recovery if events were missed.
- Deltas: per-quest updates (current_step/progress) with varints.

#### Determinism & Replay
- Debug mode allows replaying the event log to rebuild quest state.
- Hash of quest log (`state_hash`) computed after each application.

### Implementation Steps
1. Quest definition tables + validator.
2. Runtime quest log storage with hashing.
3. Server event handler that updates log deterministically.
4. Reliable encode/decode for quest events and state deltas.
5. Client ghost quest log, idempotent event processing, snapshot recovery.
6. Debug replay tool to rebuild quest log from event stream.

### Architectural Considerations
- No scripting runtime required initially.
- Replayable/deterministic: event application is pure and order-dependent; tests cover ordering.
- Reliable ordered transport; avoid malloc in hot path.
- Clear separation: networking delivers events; quest system applies and validates.

### Unit Tests (RED-first)
**Happy Path**
1. **Single quest progression**
   - kill events increment; completion advances step.
2. **Multiple quests in log**
   - events route to the correct quest(s) and update only matching ones.
3. **Completion state stability**
   - completed quests remain completed on unrelated events.
4. **Event log roundtrip**
   - encode/decode events reliably; client applies and reaches identical state.
5. **Snapshot recovery**
   - client missing events applies snapshot and continues processing deltas correctly.

**Edge Cases**
6. **Out-of-order events**
   - reliable ordered ensures application in order; test buffering and correct application.
7. **Idempotent event handling**
   - duplicate IDs ignored; state unchanged.
8. **Large progress counts**
   - high kill counts do not overflow internal counters (or clamp with explicit rule).

**Failure Modes**
9. **Invalid transition**
   - events not matching current step do not advance.
10. **Invalid quest definition**
   - cycles or invalid step references rejected by validator.
11. **Unknown quest_id**
   - updating a missing quest returns explicit error.
12. **Malformed event payload**
   - decode failure returns error; state unchanged.

### Regression Tests (RED-first)
1. **No double-advance on boundary**
   - when progress hits the completion threshold, step advances once.
2. **No cross-quest contamination**
   - events for quest A must never advance quest B.
3. **Event ordering lock-in**
   - canonical ordering yields identical state across runs.
4. **State hash determinism**
   - identical event streams produce identical `state_hash` values.

### Cumulative Integration Tests (RED-first, cumulative through P_009)
1. **Server→Client quest sync (P_000..P_009)**
   - server generates event stream; client applies events and snapshots; quest logs match.
2. **Dialogue gating via quest state**
   - after syncing quest state, dialogue options match expected gating.

---

## P_010 — Dialogue Graph System

### Design Intent
Implement node-based dialogue as a pure data graph traversal decoupled from UI. Conditions enable branching based on inventory/quest state.

### Specification
- Node: text, speaker_id
- Edge: target_node_id, condition
- Condition types:
  - `HAS_ITEM(item_id)`
  - `QUEST_STATE(quest_id, state)`
  - `ALWAYS`
- Runner:
  - holds current node
  - exposes options list and `select_option(i)` with validation

### Implementation Steps
1. Graph data structures.
2. Condition evaluation against world state snapshot.
3. Runner stepping and option filtering.

### Architectural Considerations
- UI should never mutate graph; runner owns traversal state.
- Conditions evaluated against read-only views for determinism.

### Unit Tests (RED-first)
**Happy Path**
1. **Graph traversal basic**
   - start node exposes options; selecting option advances to correct node.
2. **Conditional branches**
   - options differ based on having item.
3. **Quest-conditioned branch**
   - options differ based on quest state.

**Edge Cases**
4. **No available options**
   - runner reports “end of dialogue” state deterministically.
5. **Stable option ordering**
   - options list ordering is deterministic across runs.
6. **Self-loop / cycle handling**
   - graph cycles do not crash; runner can traverse repeatedly (or validator rejects cycles; document).

**Failure Modes**
7. **Invalid option handling**
   - selecting out-of-range returns error; node unchanged.
8. **Invalid node/edge references**
   - graph with missing target node fails validation.

### Regression Tests (RED-first)
1. **Option filtering after state change**
   - when inventory/quest state changes, the runner recomputes options correctly and does not expose stale ones.
2. **Condition evaluation purity**
   - evaluating conditions must not mutate world state; enforce via read-only views / const correctness.

### Cumulative Integration Tests (RED-first, cumulative through P_010)
1. **Dialogue driven by quest/inventory (P_000..P_010)**
   - run inventory and quest updates, then evaluate dialogue options; verify expected branches and determinism.

---

## P_011 — Flow Field AI

### Design Intent
Scale navigation to hundreds of agents by computing a single integration+flow field per target and having agents read local vectors.

### Specification
- Cost field: `uint8_t` grid (1..254 cost, 255 wall)
- Integration field: Dijkstra flood-fill from target
- Flow field: each cell points to neighbor with lowest integration

### Implementation Steps
1. Define grid storage and indexing.
2. Implement queue-based Dijkstra (binary heap optional; start with bucketed/array queue).
3. Compute flow vectors.
4. Steering system applies flow as acceleration/desired velocity.

### Architectural Considerations
- One field per target; recompute on target move threshold.
- Agents read-only; avoids per-agent A*.

### Unit Tests (RED-first)
**Happy Path**
1. **Small open grid**
   - in an empty grid, flow vectors point directly toward the target.
2. **Obstacle avoidance**
   - U-shaped wall: vectors route around opening, not through walls.

**Edge Cases**
3. **Unreachable cells**
   - cells enclosed by walls remain at “infinite/unreachable” integration and have zero/none flow.
4. **Tie-breaking determinism**
   - when multiple neighbors have equal integration, choose a deterministic priority order and test it.
5. **Cost extremes**
   - high costs (254) are avoided versus low costs; walls (255) are never entered.
6. **Boundary cells**
   - flow computation at edges does not read out of bounds.

**Failure Modes**
7. **Invalid grid sizes**
   - zero width/height returns explicit failure.
8. **Invalid target**
   - target on a wall cell returns explicit failure.

### Regression Tests (RED-first)
1. **Gradient correctness**
   - integration values strictly decrease along flow path to target (within constraints).
2. **Integration overflow guard**
   - long paths with high costs must not overflow the integration accumulator type.

### Cumulative Integration Tests (RED-first, cumulative through P_011)
1. **Compute + apply flow to agents (P_000..P_011)**
   - compute flow field in scheduled jobs, then run a steering update that moves agents along flow.
   - verify agents monotonically approach the target (or reduce integration) and results are deterministic.

---

## P_012 — Scene Management & Asset Streaming (GLTF, Lightmaps, Skybox)

### Design Intent
Provide deterministic scene graph management and robust asset streaming, integrating static lightmaps with dynamic clustered lighting, glTF 2.0 import, lightmap UV (UV2) generation and baking, support for lightmap volumes/zones, and skybox rendering. Emphasize AZDO-friendly GPU paths and minimal stalls.

### Specification
#### Scene Graph & Entities
- Scene nodes reference ECS entities (transforms, renderables, lights).
- Hierarchical transforms with baked world matrices; avoid per-frame allocations.

#### Asset Streaming
- Background IO thread reads and decompresses assets (glTF, textures) into CPU buffers.
- Staging via PBOs/persistently mapped buffers; render thread issues uploads.
- Streaming priorities: near/visible assets first; LOD-aware.

#### World Streaming & Seamless Scene Loading
- World partitioned into streaming regions/chunks with dependency metadata (references, required assets).
- Seamless transitions: prefetch adjacent regions based on player velocity/heading; unload far regions gradually.
- Cross-scene references: handle entities crossing boundaries; preserve persistent IDs and state.
- Networking integration: interest management aligns with streamed regions; clients receive only in-range entities.

#### glTF 2.0 Import
- Support meshes, materials (mapped to simplified surface shader), skeletons/skins.
- Texture formats: KTX2/PNG; generate mipmaps; sRGB handling documented.
- Import node transforms and attach to ECS components.

   Material mapping to simplified surface shader (P_016):
   - Base color → `base_color`.
   - Metallic-Roughness inputs → derive `roughness_like` and `spec_like` scalars using stable rules (e.g., `roughness_like = roughness`, `spec_like = 1 - metallic`).
   - Normal and emissive textures → map to `normal_map` and `emissive`.
   - Missing inputs → deterministic defaults; mapping is lossy but consistent and documented.

#### Animation Clips & Evaluation
- Import animation clips (channels: translation/rotation/scale) and retarget to skeletons.
- Clip storage streamed lazily; per-clip metadata includes duration, sampling rate.
- CPU-side evaluation jobs read clip keyframes, evaluate at time t (quaternion SLERP, vector LERP), produce per-bone matrices.
- Outputs staged into renderer’s bone palette buffers (see P_004 skinning).

#### Lightmaps: Baking & UV2
- Generate secondary UV (UV2) for lightmap baking; seams minimized; chart packing.
- Baking pipeline (external/offline or in-tool): produces lightmap textures + lightmap index per mesh.
- Materials support additive static lightmap sampling combined with dynamic cluster lights.

#### Lightmap Volumes & Zones
- Define volumes/zones in scene with baked ambient or probes (SH coefficients) for interiors/exteriors.
- Zone transitions: blend probes/lightmap contributions smoothly.

#### Dynamic Lighting Integration
- Clustered forward shading integrates dynamic lights with static lightmaps.
- Per-cluster light lists; materials combine static + dynamic contributions.

#### Skybox Support
- Load cubemaps (KTX2 or 6 images) or equirectangular sky textures; prefilter if needed.
- Skybox rendered first/last depending on pipeline; optional reflection probes.

### Implementation Steps
1. Scene graph data structures and ECS binding.
2. Asset IO thread + staging buffers (PBO/persistent maps) and upload scheduling.
3. glTF importer: mesh/material/skin parsing; texture loading; ECS component creation.
4. UV2 generation for meshes (atlas/pack charts); metadata stored with mesh.
5. Lightmap sampling integrated into material shader; zone/volume data bound to GPU.
6. Clustered lighting combined with lightmap in forward pass.
7. Animation clip import and storage; job scheduling for evaluation; palette staging.
8. Skybox loader and renderer; optional IBL prefilter.
9. Instrumentation: bytes streamed/frame, upload latency, GPU stalls counter; animation evaluation time; region load/unload timings.

### Architectural Considerations
- No per-frame mallocs in render path; streaming uses fixed buffers.
- Capability-based features (bindless textures, persistent maps) gated by checks.
- sRGB/linear handling documented to avoid banding.
- Deterministic scene updates for tests; avoid nondeterministic hashes.

### Unit Tests (RED-first)
**Happy Path**
1. **glTF mesh/material import**
   - parsing produces expected ECS components; materials map to shader parameters.
2. **UV2 generation validity**
   - generated UV2 in [0,1]; charts packed without overlap beyond epsilon.
3. **Lightmap sampling**
   - material combines static lightmap and dynamic light correctly in mock shader (numeric invariants).
4. **Skybox load/render setup**
   - cubemap loads; render commands issued in correct order.
5. **Streaming upload scheduling**
   - IO thread stages asset; render thread uploads without stalls; counters updated.
6. **Animation clip import + evaluation**
   - clip parsed; CPU job evaluates bones at time t; bone palette buffers receive correct matrices.
7. **Seamless region transition**
   - entering a boundary triggers prefetch; assets present before crossing; no visible pop; counters reflect expected timing.

**Edge Cases**
6. **Missing textures**
   - importer handles missing optional textures with defaults.
7. **Degenerate UVs**
   - UV2 generator handles tiny/degenerate charts without NaNs.
8. **Zone transitions**
   - probe blend clamps and transitions smoothly; no abrupt changes.
9. **Large scenes**
   - streaming prioritization respects near-visible assets; avoids starving distant assets completely.
10. **Missing animation channels**
   - clips with missing TRS channels use defaults; evaluation remains finite.
11. **Cross-scene entity handoff**
   - entity crossing region keeps persistent ID/state; duplicates not created; ownership transferred deterministically.

**Failure Modes**
10. **Malformed glTF**
   - invalid JSON/binary chunks rejected; importer returns explicit error.
11. **Upload failure**
   - missing GL entry points or buffer failures return error; renderer remains stable.
12. **Malformed animation clip**
   - invalid keyframe data rejected; evaluation returns explicit failure.
13. **Region load failure**
   - failed region load triggers retry/backoff; renderer avoids dereferencing missing resources.

### Regression Tests (RED-first)
1. **Material parameter mapping stability**
   - mapping from glTF material fields to shader uniforms remains consistent.
2. **UV2 packing determinism**
   - given fixed seed/settings, UV2 atlases identical across runs.
3. **Streaming counters**
   - bytes/frame and latency statistics remain within expected bounds under scripted loads.
4. **Animation evaluation determinism**
   - given fixed clips and timestamps, bone matrices are identical across runs.
5. **Region prefetch policy stability**
   - scripted movement yields identical region load/unload sequences.

### Cumulative Integration Tests (RED-first, cumulative through P_012)
1. **Scene load + render pipeline (P_000..P_012)**
   - import a small scene; stream assets; render with static lightmaps + dynamic clustered lights; verify deterministic command sequence via mocked GL.
2. **Zone lighting blend**
   - move camera across zone boundary; probe blend results follow expected curve.
3. **Skybox + IBL**
   - skybox renders; optional reflection probe affects materials deterministically.
4. **Animation + skinning end-to-end**
   - glTF skeleton + clip imported; job evaluates bones; renderer skins mesh; mocked GL sequence includes palette upload and draw; numeric invariants hold.
5. **World streaming + networking cohesion**
   - streamed regions align with networking interest sets; entities appear/disappear deterministically as regions load/unload.

---

## P_013 — AI & Scripting (Dynamic Modules, BT, HTN, Hierarchical Pathfinding)

### Design Intent
Provide a modular, server-authoritative AI system with runtime-loaded "scripting" via dynamic libraries (e.g., `dlopen`/`LoadLibrary`), enabling behavior trees (BT), hierarchical task network (HTN) planning, and hierarchical pathfinding. Support composable gameplay behaviors and abilities, and integrate cleanly with the quest system for deterministic, debuggable behavior.

### Specification
#### Dynamic Modules ("Scripting")
- Loader abstracts `dlopen` (POSIX) and `LoadLibrary` (Windows) with a common interface.
- Module ABI (versioned): required entry points
  - `module_init(api*)` returns `module_handle` or error.
  - `module_tick(ctx*)` for per-frame or per-agent updates.
  - `module_shutdown(module_handle)` for cleanup.
- Registration: modules register behaviors, abilities, planners via function tables.
- Safety: version checks, symbol resolution validation, and error returns; no unchecked casts.

#### Behavior Trees (BT)
- Node types: `Sequence`, `Selector`, `Parallel`, `Decorator` (Invert/Repeat), `LeafAction`.
- Blackboard: per-agent key-value store (POD, fixed capacity) with typed accessors.
- Tick semantics: nodes return `Success`, `Failure`, or `Running`.
- ECS integration: agents have `bt_component` referencing tree definition and runtime cursor state.
- Job scheduling: per-agent BT ticks run as jobs with chunking to avoid long frames.

#### HTN Planning
- Domain: tasks (compound/primitive), methods, and operators with preconditions/effects.
- Planner builds a plan given current world state (read-only snapshot) and goals.
- Replanning triggers: failure on execution, significant state changes, or time budget exceeded.
- Plan cache: memoize recent plans for similar states; invalidate on key changes.

#### Hierarchical Pathfinding
- Multi-level grid/navmesh decomposition (coarse zones → fine cells).
- High-level A* across zones; local path refinement within cells using A* or flow-guided steering (see P_011).
- Agent path component stores waypoints; steering system consumes waypoints and physics applies movement.

#### Abilities & Modular Behaviors
- Ability definitions (from modules) with conditions, cooldowns, resource costs, and effects.
- Abilities exposed as BT leaf actions and HTN operators.
- Deterministic cooldown/resource counters; ECS components track states.

#### Quest System Integration
- BT/HTN preconditions can reference quest states (via read-only view).
- Actions can emit quest-related events; integration uses reliable ordered channel (P_007) for networking.
- Deterministic traces: record BT/HTN decisions and quest events in debug mode.

#### Networking (Summary)
- Server authoritative AI state; clients replicate coarse AI state (current behavior/goal) for UI.
- Reliability policies align with P_007: behavior/goal changes reliable; minor state like timers may be unreliable.

### Implementation Steps
1. Module loader abstraction and versioned ABI; error-handling paths.
2. Behavior tree engine: node structs, tick functions, blackboard API.
3. HTN planner: domain structs, precondition/effect evaluation, planner algorithm.
4. Hierarchical pathfinding: zone graph build, A* high-level, local refinement.
5. ECS components: `bt_component`, `htn_component`, `path_component`, `ability_component`.
6. Job scheduling for BT ticks, planning, and path updates; budgets per frame.
7. Integration hooks for quest system events and predicates.
8. Networking glue for AI state replication (optional UI states).
9. Instrumentation: tick counts, planner time, nodes visited, path lengths.

### Architectural Considerations
- Determinism: BT/HTN evaluation should be pure against snapshots; avoid nondeterministic iteration orders.
- No per-tick mallocs in hot loops; use arenas and fixed-size blackboards.
- Module isolation: strict ABI, version checks, and clear ownership for resources.
- Thread safety: module `tick` may run on worker threads; forbid global mutable state unless isolated.
- Clean under `-Wall -Wextra -Wpedantic`; pure C11 plus platform loader APIs.

### Unit Tests (RED-first)
**Happy Path**
1. **Module load/unload**
   - dummy module loads via loader; init/tick/shutdown called; registration succeeds.
2. **BT sequence/selector semantics**
   - define simple trees; ticks yield expected `Success`/`Failure`/`Running` results.
3. **HTN produces valid plan**
   - given domain/goals, planner returns expected operator sequence.
4. **Hierarchical pathfinding route**
   - agent plans high-level path across zones and refines local path.
5. **Ability cooldown/resource update**
   - ability invoked updates cooldown/resource counters deterministically.
6. **Quest precondition gating**
   - BT/HTN preconditions referencing quest state gate actions appropriately.

**Edge Cases**
7. **Module version mismatch**
   - loading a module with incompatible version returns explicit error.
8. **BT decorator repeat/invert edge**
   - repeat count boundaries and invert semantics produce documented outcomes.
9. **HTN replanning triggers**
   - planner detects failed execution and replans.
10. **Unreachable path**
   - pathfinder reports no path; agent switches to fallback behavior.
11. **Blackboard capacity limit**
   - exceeding key capacity returns error; no overflow.

**Failure Modes**
12. **Module missing symbol**
   - loader fails to resolve required symbol; module rejected.
13. **Invalid ability definition**
   - malformed ability returns error; not registered.
14. **Planner precondition failure**
   - operator cannot apply; planner returns failure with explicit reason.

### Regression Tests (RED-first)
1. **BT tick determinism**
   - fixed tree and blackboard yield identical tick traces across runs.
2. **HTN plan stability**
   - given identical domain/state, resulting plan equals golden sequence.
3. **Module ABI stability**
   - function table layout and version checks locked by tests.
4. **Pathfinding performance budget**
   - nodes visited count stays under budget for scripted maps.

### Cumulative Integration Tests (RED-first, cumulative through P_013)
1. **AI drive movement (P_000..P_013)**
   - BT selects move-to behavior; HTN plans path; physics moves agent; verify deterministic positions/events.
2. **Quest-integrated behavior**
   - quests gate actions; AI emits events; quest log updates; dialogue options reflect new quest states.
3. **Networked AI state**
   - server replicates behavior/goal changes; client UI reflects; reconciliation stays consistent.
4. **Custom attributes manifest**
   - scripting module registers attributes; server replicates changes; client applies via manifest; AI decisions may read these attributes deterministically.

---

## P_014 — Platform & Input Abstraction

### Design Intent
Provide a thin, deterministic platform layer for windowing, input devices, timers, and filesystem paths to support cross-platform engine operation without entangling subsystems.

### Specification
- Window: create/destroy, resize events, swap control.
- Input: keyboard/mouse/gamepad with device IDs, button/axis states, text input; event + snapshot APIs.
- Timers: high-resolution time, sleep/yield hooks (compatible with fibers).
- Filesystem: path utilities, sandboxed asset paths, async read interface for streaming.

### Implementation Steps
1. Capability detection and platform shims.
2. Event queues and per-frame snapshots; gamepad mapping.
3. Timer functions integrated with job system.
4. Filesystem helpers and async IO bridge to streaming.

### Architectural Considerations
- No blocking calls in hot loops; poll-driven.
- Cross-platform guards; avoid compiler extensions.

### Unit/Regression/Cumulative Tests
- Events ordering deterministic; key repeat behavior documented.
- Filesystem path handling edge cases; sandbox enforcement.
- Cumulative: integrate input into ECS and networking for player commands.

---

## P_015 — Audio System (Mixer & Spatialization)

### Design Intent
Add an efficient audio mixer with buses/submixes, effects, and 3D spatialization; streaming-friendly with deterministic scheduling.

### Specification
- Voices, buses, effects chain (reverb/EQ), 3D panning/attenuation, streaming buffers.
- Asset formats decoding (e.g., WAV/OGG) via minimal decoders (or stubs for tests).

### Implementation Steps
1. Mixer graph and voice management.
2. Spatialization math and attenuation curves.
3. Streaming decode buffers; job scheduling.
4. ECS audio components; hooks for dialogue.

### Architectural Considerations
- Fixed-size buffers; avoid per-callback mallocs.
- Latency targets documented.

### Unit/Regression/Cumulative Tests
- Mix math invariants; bus levels; spatialization correctness.
- Streaming under load; dropout prevention.
- Cumulative: audio tied to scene streaming and networking events.

---

## P_016 — Render Pipeline & Surface Shader (Simplified)

### Design Intent
Provide a minimal, customizable surface shader and render pipeline plumbing (no PBR), enabling different looks via parameterized shaders without changing core renderer.

### Specification
- Surface shader parameters: base color, roughness-like factor, spec-like factor, emissive, normal map support.
- Pipeline stages: skybox, forward shading, optional post chain stub.
- Material system: lightweight parameter blocks; shader variants via defines.

### Implementation Steps
1. Define surface shader uniform/layout; compile-time variants via flags.
2. Bind material parameter blocks; texture sampling conventions.
3. Integrate with P_004 pipeline stages and scene materials.

### Architectural Considerations
- AZDO-friendly bindings; no global state.
- Deterministic parameter mapping.

### Unit/Regression/Cumulative Tests
- Shader param mapping stability; variant define behavior locked.
- Cumulative: render with skybox + forward pass using simplified shader.

---

## P_017 — ECS Serialization, Save-Load & Replay

### Design Intent
Enable deterministic save/load of ECS state and input/replay functionality for debugging and regression testing.

### Specification
- Snapshot serialization of ECS components with versioned schemas.
- Input log recording; deterministic playback.

### Implementation Steps
1. Schema registry for save/load (reuse networking schemas where applicable).
2. Snapshot writer/reader; diff tools.
3. Input recorder; replay runner.

### Architectural Considerations
- Versioning; backward-compat.
- No hidden global state.

### Unit/Regression/Cumulative Tests
- Save/load roundtrip; diff correctness.
- Replay reproduces deterministic positions/events across runs.

---

## P_018 — Asset Cooking, Hot-Reload & Virtual Filesystem

### Design Intent
Add a build-time cooking pipeline with hashed dependencies, runtime hot-reload, and a virtual filesystem to abstract asset locations.

### Specification
- Cooked cache with content hashes; dependency graph.
- VFS mounts; path resolution rules.
- Hot-reload with thread-safe staging and fallback.

### Implementation Steps
1. Cook pipeline stubs and cache index.
2. VFS mount table; path resolution.
3. Hot-reload watchers; staging + swap.

### Architectural Considerations
- Deterministic hashing; safe fallbacks on failure.

### Unit/Regression/Cumulative Tests
- Cache hit/miss; dependency invalidation.
- Hot-reload correctness under streaming.

---

## P_019 — UI, Text & Localization (Immediate Mode)

### Design Intent
Immediate-mode UI with font atlas, text shaping support (basic), and localization scaffolding.

### Specification
- Font atlas; glyph metrics; basic shaping.
- Localization tables; RTL flagging (basic).

### Implementation Steps
1. Font atlas builder; text rendering in forward pass.
2. Localization loader; string lookup.
3. Input navigation integration.

### Architectural Considerations
- Deterministic layout; avoid retained-state bugs.

### Unit/Regression/Cumulative Tests
- Glyph placement; atlas lookups; localized strings resolved.
- Cumulative: dialogue UI integrates with networking and quests.

---

## P_020 — Online Services (Matchmaking, Auth, Security)

### Design Intent
Self-hosted dedicated server model with minimal authentication and simple matchmaking; no client-side anti-cheat.

### Specification
- Auth: server-issued session tokens; straightforward handshake.
- Matchmaking: lobby directory or direct server join; player presence lists.
- Security: server-side validation and rate limiting only; no client-side anti-cheat.

### Implementation Steps
1. Simple auth handshake; issue/validate session tokens on the server.
2. Lobby directory or direct-connect endpoints; manage player presence.
3. Server-side rate limiting counters and basic validation.

### Architectural Considerations
- Dedicated server is authoritative; clients never trust unvalidated messages.
- Deterministic test harness with injected clocks; focus on simplicity and operability.

### Unit/Regression/Cumulative Tests
- Auth roundtrip; lobby join/leave; rate limit enforcement (server-side only).
- Cumulative: integrate with P_007 replication and interest management; verify dedicated server flows.

---

## End of Document