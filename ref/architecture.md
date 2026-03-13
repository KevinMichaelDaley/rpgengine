# C11-Based High-Performance Game Engine Architecture
## "Ferrum-Engine-C": A Comprehensive Design Specification

## 1. Architectural Philosophy and Core Design Principles

This report specifies "Ferrum-Engine-C," a high-performance proprietary game engine built in **pure C11** for AAA-fidelity simulation, **massive co-op multiplayer**, **dynamic world geometry**, and **high-density crowd simulation**.

The defining goals are:

- **Predictable performance:** stable frame times (60/120/144Hz targets), bounded worst-case work.
- **Deterministic behavior where required:** physics and replication logic designed to be replayable and debuggable.
- **Minimal dependencies:** C standard library + platform APIs + OpenGL bindings loaded manually.
- **Data-Oriented Design (DOD):** data layout dominates design decisions to maximize cache coherency.
- **Pure functional transformations:** systems read from input arrays and write to distinct output arrays, enabling safe parallelism and clear data flow.

### 1.0.1 Pure Functional Pipeline Stages

A core architectural principle is that **systems perform transformations in stages**, where each stage's inputs are distinct from its outputs. This follows a pure functional model:

```
Stage N:  read(input_buffer_A) → compute → write(output_buffer_B)
Stage N+1: read(output_buffer_B) → compute → write(output_buffer_C)
```

**Key properties:**

- **No aliasing:** A stage never reads and writes the same buffer. This eliminates data races and enables trivial parallelization.
- **Double-buffering:** Many systems use ping-pong buffers (e.g., `positions_prev` / `positions_curr`) to maintain temporal coherence.
- **Job decomposition:** Each stage is implemented as one or more jobs. Jobs within a stage can run in parallel because they write to disjoint regions of the output buffer.
- **Explicit dependencies:** Stage N+1 waits on Stage N's completion counter before reading its output.

**Example: Physics pipeline**
```
1. Broadphase:    read(positions_curr, aabbs) → write(collision_pairs)
2. Narrowphase:   read(collision_pairs, shapes) → write(contacts)
3. Solve:         read(contacts, velocities_in) → write(velocities_out)
4. Integrate:     read(velocities_out, positions_curr) → write(positions_next)
5. Swap:          positions_curr ↔ positions_next
```

This pattern applies across subsystems: animation evaluation, AI decisions, network state application, and rendering command generation all follow the same read-input/write-output discipline.

### 1.1 The Shift from OOP to DOD in C11
Traditional OOP "Actor" hierarchies lead to pointer chasing, cache misses, and polymorphic overhead that scales poorly with thousands of entities. Ferrum-Engine-C uses a strict **Entity Component System (ECS)** to separate:

- **Identity:** entity handles (index + generation)
- **Data:** components stored contiguously
- **Behavior:** systems operating on homogeneous streams of data

This enables high-throughput loops with predictable memory access patterns and straightforward parallelization.

### 1.2 The Concurrency Model: Beyond Standard Threads
Ferrum-Engine-C does not adopt "one OS thread per subsystem." Instead it uses a **fiber-based job system** inspired by modern AAA engine architectures:

- A small pool of **pinned worker threads**
- Thousands of **user-space fibers** (stackful coroutines)
- Work expressed as fine-grained **jobs**
- Dependencies managed via **counters/events**, not blocking calls

This model minimizes kernel context switches and keeps cores busy even when tasks wait on dependencies.

**Exception: networking and IO boundaries.**
Networking must interact with OS sockets and OS scheduling. On the **client**, the network subsystem runs on dedicated OS thread(s) that do packet IO, reliability/reassembly, and then feed decoded messages into fiber-safe jobs. On the **server**, networking is modeled as one fiber per connected client, scheduled by the job system, with per-client channels bridging to simulation.

### 1.3 Engine Settings (Frozen-at-Launch Configuration)

Global engine configuration is managed via `engine_settings.h`:
- `fr_engine_settings_init()` -> `fr_engine_settings_mut()` to configure ->
  `fr_engine_settings_freeze()` -> `fr_engine_settings_get()` (read-only).
- After `freeze()`, all writes are rejected — the settings become immutable
  before any threads or job systems start.
- Currently holds network emulation parameters (gated by `FR_NET_EMULATION`).
- Designed to hold any application-wide configuration that must be set once
  at startup and never changed at runtime.

### 1.4 Networking as a First-Class Citizen
Massive co-op with client-side prediction and dynamic geometry requires networking to be core architecture, not a bolt-on. The engine uses:

- **Server authoritative simulation**
- **UDP transport with reliability channels**
- **Delta compression and snapshot replication**
- **Chunk-based dynamic geometry propagation** for terrain edits/building

---

## 2. Directory Structure

The codebase is split between public headers (`include/ferrum/`) and source files (`src/`), with deep hierarchies following the directory-first design principle.

### 2.1 Top-Level Layout

```
include/ferrum/          Public headers
  aegis/                 Aegis scripting VM
  animation/             Skeletal animation, ragdoll, IK, constraints
  ecs/                   Entity Component System
  editor/                Editor subsystems (scene, mesh, UI, viewport)
  entity/                Entity attributes and event flags
  job/                   Fiber-based job system
  math/                  Vector, matrix, quaternion math
  memory/                Arena, pool, VM allocators
  mesh/                  OBJ mesh loader
  net/                   Networking (RUDP, channels, replication)
  physics/               Physics world, bodies, joints, collision
  renderer/              Render pipeline, shaders, meshes, skinning
  server/                Server tick loop, networking, physics sync

src/                     Implementation files (mirrors include/ hierarchy)
  aegis/                 Aegis VM, runtime, ops/
  animation/             Animation solver, ragdoll, format loaders
  ecs/                   Sparse set, entity pool, world
  editor/                Editor (commands, mesh, scene, UI, viewport)
  entity/                Entity attribute tables
  job/                   Job system, work-stealing deque, fibers
  math/                  Math implementations
  memory/                Arena, pool, alloc_tracy/
  mesh/                  Mesh loaders
  net/                   Networking (channel, client, rudp, replication, topic)
  physics/               Physics (body, broadphase, collision, constraint,
                           muscle, solver, stages, tier, world)
  renderer/              Renderer (draw, gltf, mesh, scene, skinning, ubo,
                           video_capture, debug_lines)
  server/                Server (entity, net, physics, repl, tick)
```

---

## 3. Core Foundation: Fiber-Based Job System

Source: `src/job/`, Headers: `include/ferrum/job/`

### 3.1 The Fiber Abstraction
A **fiber** is a lightweight execution context with its own stack, cooperatively scheduled atop an OS thread. Context switching is done in user-space by saving/restoring registers and stack pointer.

**Key properties**
- Cooperative (explicit yield), deterministic scheduling possible in debug mode
- Fixed stacks (typically 256KB for sim fibers, 256KB for net fibers), recycled from pools via `apool_t`
- **Tracy builds require larger stacks** — Tracy instrumentation adds significant frame overhead; use >= 128KB for any fiber that runs instrumented code
- Fiber stacks are allocated in a separate pool from fiber context/metadata to preserve debuggable call stacks
- GCC stack-protector canary (`%fs:0x28` on x86-64) is saved/restored across context swaps to prevent false stack-smash detection (`stack_canary.h`)
- No blocking on OS primitives inside fibers; waiting yields to scheduler

### 3.2 Implementation

Key files:
- `include/ferrum/job/system.h` — `job_system_t`, worker init, dispatch API
- `include/ferrum/job/counter.h` — atomic dependency counters
- `include/ferrum/job/spinlock.h` — `job_spinlock_t` (CAS spinlock for fiber contexts)
- `include/ferrum/job/ws_deque.h` — work-stealing deque
- `include/ferrum/job/instrumentation.h` — Tracy zone instrumentation

The job system initializes **N worker threads** at startup (typically = physical cores). Each worker runs:

1. Pop a ready job from its local deque
2. Execute the job on a fiber
3. If job waits, fiber yields; worker immediately continues with other work

If its local queue is empty, the worker **steals** jobs from other workers' tails.

### 3.3 Dependencies and Safety
- **Atomic counters** represent outstanding work
- Parent job spawns children: increments counter; children decrement on completion
- Waiting does **not block**: it parks the current fiber until the counter reaches zero
- Fiber-code paths use `job_spinlock_t` instead of C11 `mtx_t` to avoid blocking OS threads
- OS-thread sleep/wake uses `pthread_mutex_t` + `pthread_cond_t`
- Fiber wait/wake uses a 4-state atomic protocol (`RUNNING -> WAITING -> SIGNALED -> RUNNING`)

---

## 4. Memory Architecture: Arenas and Deterministic Allocation

Source: `src/memory/`, Headers: `include/ferrum/memory/`

### 4.1 Allocator Types

| Allocator | Header | Purpose |
|-----------|--------|---------|
| `arena_t` | `arena.h` | Bump allocator for transient/frame data |
| `pool_t` | `pool.h` | Fixed-size block pool for components |
| `apool_t` | `apool.h` | Aligned pool for fiber stacks |
| `vm_alloc_t` | `vm_alloc.h` | Virtual memory page allocator |

- **Thread-safe arena allocator** with atomic bump pointer for multi-worker use.
- Tracy memory tracking integration via `alloc_tracy/`.

### 4.2 Frame Arenas (Bump Allocators)
Per-worker **frame arena**:
- Used for transient data (visibility lists, temporary collision pairs, UI vertex buffers)
- Reset at end of frame
- Alignment-safe allocations

### 4.3 Level Arenas
A **level arena** holds data valid for the duration of a map:
- Static terrain meshes, navigation data, loaded entity state
- Freed wholesale during map transition

### 4.4 Integration with ECS
ECS storage is designed to be arena-backed or pool-backed to control fragmentation:
- Stable component pools for long-lived components
- Dedicated arenas for high-churn subsystems (particles, projectiles)

---

## 5. Entity Component System (ECS) Implementation

Source: `src/ecs/`, Headers: `include/ferrum/ecs/`

Ferrum-Engine-C uses a **hybrid ECS** combining sparse sets for general entities with **homogeneous entity pools** for high-throughput batch processing.

### 5.1 Core Types

- `entity_t` — Entity handle with index + generation counter (`entity.h`)
- `ecs_sparse_set_base_t` — Generic sparse set storage (`sparse_set.h`)
- `ecs_world_t` — World registry owning entity pool and component stores (`world.h`)
- `ecs_entity_pool_t` — Entity allocation pool with generation tracking

### 5.2 Sparse Set Storage (General Entities)

- **O(1) lookup:** sparse array maps entity index -> dense index
- **O(1) add/remove:** swap-and-pop removal, append insertion
- **Cache-friendly iteration:** dense arrays are contiguous
- **Generation counters:** detect use-after-destroy via entity handles

### 5.3 Homogeneous Entity Pools (Batch Processing)

For compute-intensive entities with fixed component sets, dedicated **pool-per-type** storage provides maximum throughput.

### 5.4 Entity Attributes

Source: `src/entity/`, Headers: `include/ferrum/entity/`

- `entity_attrs.h` — Per-entity attribute key-value table
- `entity_event_flags.h` — Event flag bitmask for entity state changes

### 5.5 Systems as Pipeline Stages

Systems follow the pure functional transformation model:
- **Input/output separation:** Systems read from source arrays, write to destination arrays
- **Parallel chunking:** Large pools are split across jobs
- **Counter-based dependencies:** Stage N+1 waits on Stage N's completion counter

---

## 6. Math Library

Source: `src/math/`, Headers: `include/ferrum/math/`

| Header | Contents |
|--------|----------|
| `vec2.h` | 2D vector operations |
| `vec3.h` | 3D vector operations |
| `vec4.h` | 4D vector operations |
| `quat.h` | Quaternion math (multiply, rotate, slerp, conjugate) |
| `quat_angle.h` | Quaternion-angle conversions, decomposition |
| `mat4.h` | 4x4 matrix (transform, projection, inverse) |
| `constants.h` | Pi, epsilon, common constants |
| `common.h` | Shared utility macros (min, max, clamp) |

---

## 7. Physics and Simulation Architecture

Source: `src/physics/`, Headers: `include/ferrum/physics/`

The engine implements its own full-featured physics subsystem with no third-party dependencies. It uses a deterministic fixed timestep (default 60Hz) with configurable substeps.

### 7.1 Physics World

`phys_world_t` (`world.h`) is the top-level container owning:

- **Double-buffered body pool** (`phys_body_pool_t`) for lock-free read during render
- **Collider array** and per-body AABBs
- **Shape storage:** spheres, boxes, capsules, mesh shapes, convex hulls, halfspaces, compound convex shapes, point colliders
- **Persistent manifold cache** with warmstarting
- **Per-frame arena** for transient collision data
- **Persistent static BVH** for static geometry acceleration
- **Joint array** for constraint-based connections
- **Collision exclusion set** for animation bones and joint-connected bodies
- **Impact event buffer** for gameplay callbacks
- **Animation substep callbacks** for kinematic/motor-driven animation integration

Configuration via `phys_world_config_t` controls pool sizes, solver parameters, sleep thresholds, CCD, substep count, island limits, and XPBD compliance.

### 7.2 Collision Detection

**Broadphase** (`src/physics/broadphase/`):
- Spatial hash grid (`spatial_grid.h`)
- Static BVH for immovable geometry (`static_bvh.h`)
- Parallel broadphase (`broadphase_par.h`)

**Narrowphase** (`src/physics/collision/`):
- Primitive pair tests: box-box, box-capsule, capsule-capsule, sphere simplification
- Halfspace collision
- GJK/EPA for convex hull pairs (`gjk_epa.h`, `gjk_support.h`)
- Mesh narrowphase via BVH traversal (`mesh_narrowphase.h`)
- Convex decomposition for mesh approximation (`convex_decompose.h`)
- Compound collider support (`compound_collider.h`, `convex_compound.h`)
- Speculative contacts and CCD (`ccd.h`, `ccd_dynamic.h`)

**Contact management:**
- Manifold build and caching (`manifold.h`, `manifold_cache.h`, `manifold_build.h`)
- Cache commit with warmstarting decay (`cache_commit.h`)
- Impact event generation above configurable threshold

### 7.3 Constraint Solver (Dual Solver Architecture)

The physics engine uses a **tiered dual-solver** architecture:

- **TGS (Temporal Gauss-Seidel)** (`tgs_solve.h`): velocity-level solver for T0/T1 tier bodies (high-accuracy stacking, contacts). Operates per-island.
- **XPBD (Extended Position-Based Dynamics)** (`xpbd_solve.h`): position-level solver for T2-T4 tier bodies (unconditionally stable, no island decomposition required).
- **CG (Conjugate Gradient)** solver (`solver/cg_solve.h`, `solver/cg_types.h`): optional linear system solver for improved convergence on stiff joint chains.

Supporting infrastructure:
- Island decomposition and graph coloring (`island.h`, `island_build.h`, `constraint_color.h`)
- Tier classification (`tier_classify.h`, `tier_list.h`) to assign bodies to the appropriate solver
- Solver transition logic (`solver_transition.h`)
- Position projection for post-solve stabilization (`position_projection.h`)
- Baumgarte stabilization (`stabilization.h`)
- Velocity sync between solver tiers (`velocity_sync.h`)

**Parallel stages** (`src/physics/stages/par/`):
- `broadphase_par.h`, `narrowphase_par.h`, `manifold_build_par.h`
- `constraint_build_par.h`, `tgs_solve_par.h`, `xpbd_solve_par.h`
- `integrate_par.h`, `spatial_update_par.h`, `stabilization_par.h`
- `tier_classify_par.h`, `collision_fused_par.h`

### 7.4 Joint System

The joint system (`joint.h`) supports 11 joint types:

| Type | Description | Rows |
|------|-------------|------|
| `PHYS_JOINT_DISTANCE` | Spring-damper distance constraint | 1 |
| `PHYS_JOINT_BALL` | 3-DOF ball/spherical joint | 3 |
| `PHYS_JOINT_HINGE` | 1-DOF revolute joint | 5 |
| `PHYS_JOINT_LOCK` | 0-DOF rigid attachment | 6 |
| `PHYS_JOINT_COPY_ROTATION` | Match orientation | 3 |
| `PHYS_JOINT_LIMIT_ROTATION` | Per-axis angular limits | up to 3 |
| `PHYS_JOINT_LIMIT_POSITION` | Per-axis positional limits | up to 3 |
| `PHYS_JOINT_AIM` | Track-to/damped-track | 2 |
| `PHYS_JOINT_IK` | IK chain pair | 3 |
| `PHYS_JOINT_CONE_TWIST` | Ball + per-axis angular limits | 3-6 |
| `PHYS_JOINT_TWIST` | Single-axis twist rotation | 5-6 |

Joint features:
- Compliance (XPBD), damping, warmstarting via cached lambdas
- Yield and break strength (permanent deformation / removal thresholds)
- Angular and linear drive flags for soft return-to-rest behavior
- Rest-pose relative orientation for limit reference frames
- CG solver mass scaling for improved joint chain conditioning

### 7.5 Joint Drivers

`joint_driver.h` provides 5 actuation behavior types that modify constraint rows:

| Driver | Behavior |
|--------|----------|
| `PHYS_DRIVER_MOTOR` | Constant-velocity motor |
| `PHYS_DRIVER_SPRING` | Restoring spring with damping |
| `PHYS_DRIVER_LINEAR_ACTUATOR` | Position-targeting with speed limit |
| `PHYS_DRIVER_SERVO` | PD (proportional-derivative) controller |
| `PHYS_DRIVER_AERO_HYDRAULIC` | Velocity-dependent drag / flow-limited force |

### 7.6 Biomechanical Muscle System

Source: `src/physics/muscle/`, Headers: `include/ferrum/physics/muscle/`

A biomechanical muscle model for physically-driven character animation:

- `activation.h` — First-order activation dynamics (excitation -> activation)
- `force_curve.h` — Hill muscle force model (force-length-velocity curves)
- `tendon.h` — Series elastic tendon element
- `geometry.h` — Muscle attachment points, wrapping surfaces, moment arm computation
- `muscle_unit.h` — Composite muscle unit evaluating the full pipeline to produce joint torque
- `muscle_pair.h` — Agonist/antagonist muscle pairs for bidirectional actuation

### 7.7 Additional Physics Features

- **Queries:** raycasting (`raycast.h`), overlap tests (`overlap.h`, `phys_overlap.h`), closest point (`closest_point.h`)
- **Prediction mode:** skips collision response for client-side prediction (`prediction.h`)
- **Snapshots:** physics state serialization for networking (`snapshot.h`)
- **Deferred mutations:** command-based body mutation applied atomically at tick end (`phys_cmd.h`)
- **Amortized computation:** spread expensive recalculations across ticks (`amortized.h`)
- **Step planning:** variable substep scheduling (`step_plan.h`)
- **Physics job integration:** `phys_jobs.h`, `phys_tick_runner.h` for parallel tick dispatch

---

## 8. Animation System

Source: `src/animation/`, Headers: `include/ferrum/animation/`

### 8.1 Skeleton and Animation Data

- `fskel_format.h` / `fskel_loader.h` — Custom skeleton file format and loader
- `bone_to_body.h` — Bone-to-physics-body mapping
- `bone_collider.h` — Per-bone collision shape descriptors
- `bone_joint_desc.h` — Per-bone joint configuration for ragdoll generation
- `bone_muscle_desc.h` — Per-bone muscle attachment descriptors
- `transform_map.h` — Skeleton transform hierarchy
- `copy_track.h` — Copy constraint track evaluation

### 8.2 Ragdoll System

`ragdoll.h` — Full ragdoll with bone-to-body mapping and motor-driven animation:

- Creates one physics body per bone (capsule-shaped)
- Ball joints between parent-child bones
- Motor targets from animation solver output (per-bone strength 0.0-1.0)
- Bidirectional sync: animation -> motor targets, physics -> bone world transforms

### 8.3 Physics-Animation Constraint Solver

- `constraint_solver.h` — Constraint evaluation for animation
- `constraint_types.h` / `constraint_params.h` — Constraint type definitions
- `constraint_space.h` — Local/world space conversions
- `anim_constraint_rows.h` — Animation constraint row builders
- `limit_constraints.h` — Joint limit constraint helpers
- `ik_solver.h` — Inverse kinematics solver
- `surface_vol.h` — Surface volume calculations for muscle wrapping

### 8.4 GPU Skinning

GPU skinning is required for crowd scale:
- Bone palette uploaded via UBO/SSBO (`bone_palette.h`)
- Vertex shader applies weighted bone transforms
- Skinning pipeline in `include/ferrum/renderer/skinning/`

---

## 9. Rendering Pipeline

Source: `src/renderer/`, Headers: `include/ferrum/renderer/`

Rendering targets OpenGL 4.6 with "Approaching Zero Driver Overhead" (AZDO) patterns:
- Direct State Access (DSA)
- Persistent mapped buffers where appropriate
- Multi-draw indirect (optional) for large crowds
- Careful state change minimization

### 9.1 GL Abstraction Layer

- `gl_loader.h` — Runtime GL function pointer resolution (no GLEW dependency in core)
- `gl_constants.h` — GL enum constant definitions

### 9.2 Render Pipeline Graph (9-Pass Architecture)

`render_pipeline.h` defines an ordered 9-pass pipeline:

| Pass | Type | Purpose |
|------|------|---------|
| 0 | `RENDER_PASS_SHADOW` | Per-light shadow map generation |
| 1 | `RENDER_PASS_DEPTH_PRE` | Optional depth pre-pass for early-Z |
| 2 | `RENDER_PASS_CASTER` | Precomputed shadow maps for static lights |
| 3 | `RENDER_PASS_LIGHT_CULL` | Tiled light assignment |
| 4 | `RENDER_PASS_FORWARD` | Main shading: geometry + lighting |
| 5 | `RENDER_PASS_SKYBOX` | Drawn at max depth after forward |
| 6 | `RENDER_PASS_DEBUG` | Debug lines, gizmos, wireframes |
| 7 | `RENDER_PASS_POST` | Tone mapping, gamma, FXAA |
| 8 | `RENDER_PASS_UI` | 2D overlay |

Each pass has optional begin/submit/end callbacks, a per-pass draw list, and an FBO binding. Pipeline execution is deterministic.

### 9.3 Draw List and Sort Key System

Source: `src/renderer/draw/`

- `draw_list.h` — Flat array of draw commands for a single render pass, cleared and reused each frame
- `draw_sort.h` — 64-bit packed sort keys (shader > material > mesh > depth) for state-change minimization
- O(n) radix sort on 64-bit keys (8-pass, stable)

Each `draw_command_t` contains: sort key, mesh handle, submesh index, instance offset/count.

### 9.4 Mesh Infrastructure

Source: `src/renderer/mesh/`, Headers: `include/ferrum/renderer/mesh/`

- `mesh_handle.h` — Opaque handle type with index + generation for stale reference detection
- `mesh_registry.h` — Central mesh store mapping handles to static/skeletal mesh data
- `static_mesh.h` — Static mesh (VAO + index/vertex counts)
- `skeletal_mesh.h` — Skeletal mesh (static mesh + bone data)

The mesh registry uses a freelist for O(1) alloc/free and supports both mesh types in a single flat array of union slots.

### 9.5 Shader System

- `shader_program.h` — Shader compilation, linking, and binding wrapper with explicit GL function pointers
- `shader_uniforms.h` — Cached uniform location lookup

### 9.6 VAO/VBO Primitives

- `vao.h` / `vao_attribute.h` — Vertex Array Object wrapper with attribute binding
- `vbo.h` — Vertex/Index Buffer Object wrapper

### 9.7 UBO System

Source: `src/renderer/ubo/`

- `frame_params_ubo.h` — Per-frame parameters (view, projection, camera position)
- `instance_data_ubo.h` — Per-instance transform data

### 9.8 Scene Graph

Source: `src/renderer/scene/`

- `scene_graph.h` — Hierarchical scene node tree
- `scene_node.h` — Individual scene node with transform and mesh reference

### 9.9 glTF Import

Source: `src/renderer/gltf/`

- `gltf_loader.h` — glTF 2.0 mesh/skeleton/material import

### 9.10 Skinning Pipeline

Source: `src/renderer/skinning/`

- `skinning.h` — Top-level skinning API
- `skinning/skin.h` — Skin instance data
- `skinning/pipeline.h` — GPU skinning dispatch
- `skinning/components.h` — ECS skinning components
- `skinning_shader.h` — Skinning shader source
- `bone_palette.h` — Bone matrix upload

### 9.11 Debug Rendering

Source: `src/renderer/debug_lines/`

- `debug_lines.h` — Immediate-mode debug line rendering
- `debug_correction_lines.h` — Physics correction visualization

### 9.12 Video Capture Module

Source: `src/renderer/video_capture/`

GPU-buffered video capture (`fr_video_capture_t`) records the framebuffer to MP4 without stalling the render loop:
- 4-slot PBO ring for async GPU->CPU readback (fence-sync, non-blocking)
- 8-slot SPSC lock-free frame ring for render->encode thread transfer
- Dedicated encode pthread pipes raw RGBA to ffmpeg (H.264 / libx264)
- Render-side decimation to target FPS (default 30)

### 9.13 Simplified Surface Shader (Non-PBR)
Materials use a minimal parameter set:
- `base_color`, `roughness_like`, `spec_like`, `emissive`, optional `normal_map`
- glTF metallic-roughness mapped deterministically

---

## 10. Aegis Scripting VM

Source: `src/aegis/`, Headers: `include/ferrum/aegis/`

Aegis is a custom bytecode virtual machine for game scripting with a coroutine-based execution model.

### 10.1 VM Architecture

- **Register-based:** 256 x 128-bit general-purpose registers (4 KB register file, L1-friendly)
- **Fixed-width instructions:** 128-bit (4 x uint32_t) per instruction
- **Coroutine model:** resume with event, execute until yield/exit/fuel exhaustion, return update set
- **Fuel-limited execution:** configurable instruction budget per resume to prevent runaway scripts

Register union supports: i32, i64, f32, f64, vec2, vec3, vec4 (quaternion), entity_id, handle, raw bytes.

### 10.2 Instruction Set

74+ opcodes organized by category:

| Category | Opcodes |
|----------|---------|
| Coroutine control | YIELD, RESUME, EXIT |
| Function calls | CALL, RET |
| Async operations | WAIT, POLL |
| Event access | EVENT_TYPE, EVENT_SRC, EVENT_FIELD |
| World queries | QUERY_ENTITY, GET_ATTR, ENTITY_COUNT, ENTITY_AT |
| Async world queries | VIS_TEST (raycast), NAV_QUERY (navmesh) |
| State mutation | BUILD_UPDATE, TARGET_ENTITY, SET_FIELD, ADD_HINT, PUSH_UPDATE |
| Data movement | MOV, LOAD_IMM, LOAD_IMM64 |
| Integer arithmetic | ADD, SUB, MUL, DIV, MOD, NEG |
| Float arithmetic | FADD, FSUB, FMUL, FDIV, FNEG |
| Bitwise/logic | AND, OR, XOR, NOT |
| Comparison | EQ, NE, LT, LE, GT, GE (integer), FLT, FLE, FGT, FGE (float) |
| Control flow | JMP, JMP_IF, JMP_IF_NOT |
| Memory | ALLOC, LOAD, STORE, STATIC_LOAD, STATIC_STORE, PUSH, POP |
| Type conversion | I32_TO_F32, F32_TO_I32, I64_TO_F64, F64_TO_I64, F64_TO_F32, F32_TO_F64 |
| Vector/quat math | VEC3_ADD/SUB/MUL/SCALE/DOT/CROSS/LEN/NORM, QUAT_MUL, QUAT_ROTATE, VEC3_PACK |
| Event signaling | SIGNAL (rate-limited), SUBSCRIBE, AWAIT_EVENT |
| Environment | CLOCK, SIN, COS |
| Debug | SHOW |

### 10.3 Memory Model

Three-zone memory per VM (`aegis_memory.h`):
- **Static zone:** persistent across yields (global variables)
- **Heap zone:** bump-allocated, reset on yield (temporary data)
- **Call stack:** PUSH/POP for function call frames

### 10.4 Runtime System

`aegis_runtime.h` manages script lifecycle:

- **Script registry:** up to 128 registered scripts, lazily spawned on first matching event
- **Script instances:** each gets own VM, event queue, and arena buffer
- **Fiber integration:** scripts dispatch as long-lived fibers on the job system; force-yield maps to `job_yield()`
- **Topic-based pub/sub:** `aegis_topic_table` routes events to subscribed scripts
- **Rate limiting:** per-script signal rate limiting to prevent event storms
- **Idle tracking:** auto-unschedule scripts that exited and haven't received events within grace window
- **Async task buffer:** shared buffer for VIS_TEST/NAV_QUERY results
- **Entity snapshot view:** read-only entity state for world queries, updated each tick

### 10.5 Assembler

`aegis_asm.h` — Text assembler for compiling human-readable mnemonics to bytecode.
Source: `src/aegis/aegis_asm_parse.c`, `src/aegis/aegis_asm_compile.c`

### 10.6 Operation Dispatch

Source: `src/aegis/ops/` — 20+ files, one per opcode category:
- Arithmetic, float arithmetic, comparison, float comparison
- Data movement, flow control
- Entity queries, entity iteration
- Event handling, signal, poll, await
- Async operations
- State update, update push
- Vec3, quaternion math
- Type conversion

---

## 11. Editor

Source: `src/editor/`, Headers: `include/ferrum/editor/`

The editor is a substantial subsystem providing both a TUI (terminal) editing interface and a graphical scene editor.

### 11.1 Scene Editor (Graphical)

Source: `src/editor/scene/`, Headers: `include/ferrum/editor/scene/`

A standalone SDL2/OpenGL application with Clay UI that connects to a game server:

- `scene_main.h` — Top-level context (`scene_editor_t`) owning SDL2 window, GL context, Clay UI backend, panel layout, entity store, selection, viewport renderer, and server connection
- `scene_panel.h` — Four-region panel layout with resizable dividers
- `scene_ui.h` — Interactive UI state (actions, scroll, mouse)
- `scene_input.h` — SDL2 event dispatch
- `scene_frame.h` — Per-frame update logic
- `scene_connection.h` — TCP/UDP connection to game server
- `scene_sync.h` — Offline queue and entity state synchronization
- `scene_cmd.h` — Scene editor command dispatch
- `snap_state.h` — Grid/snap state for precise positioning

### 11.2 3D Viewport Rendering

Headers: `include/ferrum/editor/scene/scene_viewport_render.h`

Off-screen framebuffer rendering displayed in Clay UI:
- FBO with color texture + depth renderbuffer, resized dynamically
- Full 9-pass render pipeline integration
- Blinn-Phong entity shader + grid line shader
- Mesh registry for entity visualization (box, sphere, capsule, plane primitives + loaded FVMA meshes)
- Orbit camera with mouse-driven navigation

### 11.3 Clay UI System

Headers: `include/ferrum/editor/ui/`

- `clay_backend.h` — OpenGL rendering backend for Clay UI
- `clay_fonts.h` — Font atlas management
- `clay_theme.h` — Color theme definitions
- `glad_gl_loader.h` — GL loader for editor context

### 11.4 Viewport Interaction

Headers: `include/ferrum/editor/viewport/`

- `viewport_camera.h` — Editor orbit camera (pitch, yaw, distance, target)
- `viewport_gizmo.h` — Transform gizmo rendering
- `selection_raycast.h` — Mouse-to-world raycasting for entity picking

### 11.5 Editor Panels

Headers: `include/ferrum/editor/panels/`

- `inspector_widgets.h` — Property inspector UI widgets
- `outliner_tree.h` — Entity hierarchy tree view
- `panel_toolbar.h` — Toolbar panel

### 11.6 Command System

Source: `src/editor/commands/`

The editor uses a JSON-RPC-style command dispatch system:

- `edit_dispatch.h` — Command dispatch table with handler registration
- `edit_cmd_ctx.h` — Command execution context
- `edit_cmd_ring.h` — Lock-free command ring buffer for async command submission
- `edit_commands.h` — 50+ registered commands including:
  - Entity operations: spawn, delete, clone, move, rotate, scale, select/deselect
  - Selection: by ID, regex pattern, proximity, touching, flood-fill, groups
  - Scene I/O: save, load, source (batch command file)
  - Physics control: pause, resume, step, reset, material assignment, joint creation
  - Asset management: list, search, complete, browse
  - Mesh modeling: create primitives, mode switch, extrude, inset, bevel, select elements, commit
  - Scripting: script load/unload/list (Aegis integration)
  - Aliases and cursor: named position markers, cursor push/pop/snap

### 11.7 Undo/Redo System

`edit_undo.h` — Ring-buffer undo stack with dedicated snapshot arena:
- Forward/inverse command pairs recorded at mutation time
- Grouped undo (multiple commands reversed atomically)
- 16 MB default snapshot arena for entity state storage
- Delta storage for simple ops (move/rotate) avoids full snapshots

### 11.8 Mesh Editing Subsystem

Source: `src/editor/mesh/`, Headers: `include/ferrum/editor/mesh/`

Full mesh modeling toolkit:

| Module | Purpose |
|--------|---------|
| `mesh_slot.h` | Editable mesh slot (vertex/index data) |
| `mesh_edit.h` | Top-level mesh editing (16 simultaneous slots, selection bitsets) |
| `mesh_selection.h` / `mesh_select.h` | Element selection (vertex/edge/face/polygroup/object modes) |
| `mesh_commit.h` | Commit edited mesh to world entity |
| `mesh_vao_format.h` | FVMA vertex format for GPU upload |
| `mesh_snapshot.h` | Mesh state snapshot for undo |
| `mesh_undo.h` | Mesh-specific undo integration |
| `mesh_primitives.h` | Procedural primitives (box, sphere, cylinder, plane) |
| `mesh_extrude.h` | Face extrusion |
| `mesh_inset.h` | Face inset |
| `mesh_bevel.h` | Edge beveling |
| `mesh_bridge.h` | Edge/face bridging |
| `mesh_subdivide.h` | Subdivision |
| `mesh_merge.h` | Vertex/mesh merging |
| `mesh_clip.h` | Mesh clipping |
| `mesh_csg.h` | Constructive solid geometry operations |
| `mesh_brush.h` | Brush-based sculpting |
| `mesh_normals.h` | Normal recalculation |
| `mesh_triangulate.h` | Polygon triangulation |
| `mesh_transfer.h` | Mesh data transfer between slots |
| `mesh_material.h` / `mesh_material_ops.h` | Per-face material assignment |
| `mesh_uv.h` | UV coordinate editing |
| `mesh_uv_pack.h` | UV atlas packing |
| `mesh_uv_seam.h` | UV seam marking |
| `mesh_uv_smart.h` | Smart UV projection |
| `mesh_uv_transform.h` | UV transform operations |

### 11.9 Editor Mode System

Headers: `include/ferrum/editor/mode/`

- `mode_manager.h` — Editor mode state machine
- `mode_object.h` — Object manipulation mode

### 11.10 Asset Registry

`edit_asset_registry.h` — Server-side asset catalog:
- Recursive directory scanning with type detection from file extension
- Asset types: mesh (.glb, .obj), texture (.png, .ktx2, .jpg), material, prefab, script
- Prefix listing, regex search, path completion
- CRC32 content hash for change detection

### 11.11 Editor Networking

Source: `src/editor/client/`, Headers: `include/ferrum/editor/client/`

Client-side editor networking:
- `client_state_socket.h` / `client_state_dispatch.h` — State synchronization socket
- `client_asset_cache.h` / `client_asset_download.h` — Asset download and caching
- `client_mesh_render.h` — Remote mesh rendering
- `client_cursor.h` — Collaborative cursor state
- `client_editor_input.h` — Input event forwarding

### 11.12 Additional Editor Infrastructure

- `editor_ctx.h` — Editor context aggregator
- `edit_entity.h` — Editor entity store (local mirror of server state)
- `edit_selection.h` — Multi-entity selection state
- `edit_history.h` — Command history log
- `edit_serialize.h` — Level serialization (JSON)
- `edit_io_thread.h` — Background I/O for save/load
- `edit_physics_ctrl.h` — Physics simulation control (pause/step/reset)
- `edit_script_env.h` / `edit_script_rebase.h` — Aegis script integration
- `json_parse.h` — Lightweight JSON parser for command args
- `protocol/edit_autosave.h` — Autosave protocol

### 11.13 TUI Editor

Source: `src/editor/controller/`

- `ctrl_tui.h` — Terminal UI interface
- `ctrl_cmd_defs.h` — TUI command definitions
- `ctrl_conn.h` — TUI connection management
- `ctrl_browse.h` — TUI asset browser
- `ctrl_mesh_mode.h` — TUI mesh editing mode
- `ctrl_log.h` — TUI log display

---

## 12. Networking Architecture: Massive Co-op

Source: `src/net/`, Headers: `include/ferrum/net/`

For a more detailed description of the runtime message flow and channel abstractions, see `ref/networking_runtime.md`.

### 12.1 Transport Layer: UDP with Reliability Channels

- `udp_socket.h` — Platform socket wrapper (`src/net/udp/`)
- `reliable_channel.h` — Reliable delivery with acknowledgments
- `reliable_ordered_channel.h` — Reliable ordered delivery
- `unreliable_channel.h` — Best-effort delivery for high-rate state
- `ack_window.h` — Sliding ACK window for reliability tracking

**RUDP implementation** (`src/net/rudp/`):
- `peer.h` — Per-peer RUDP state
- `reliability.h` / `reliability_send.h` — Retransmission logic
- `wire_frame.h` — Wire-level frame format
- `stream.h` — Byte stream for serialization

**Network condition emulation** (`src/net/emulation/`):
- `net_emulator.h` — In-process delay queue simulating latency, jitter, loss, reorder, duplication
- Enabled via `FR_NET_EMULATION` / `make EMU=1`

### 12.2 Topic/Channel System

Source: `src/net/topic/`, `src/net/channel/`

- `topic_channel.h` — Named pub/sub channels
- `topic_dispatcher.h` — Message routing to subscribers

### 12.3 Replication

Source: `src/net/replication/`, Headers: `include/ferrum/net/replication/`

- `body_state.h` / `body_state_batch.h` — Rigid body state replication
- `body_spawn.h` / `spawn.h` / `spawn_batch.h` — Entity spawn messages
- `body_state_inbox.h` — Inbound state queue
- `event_batch.h` — Batched event replication
- `join.h` / `welcome.h` — Connection handshake
- `mesh_data.h` — Mesh data replication
- `prediction_tick.h` — Client prediction tick tracking
- `state_cube.h` — State cube for interest management

**Quantization and compression:**
- `quantization.h` — Float quantization utilities
- `quat_smallest3.h` — Quaternion smallest-3 compression
- `quat_snorm16.h` — SNORM16 quaternion encoding
- `vec3_mm.h` — Min/max bounded vec3 quantization
- `bit_pack.h` — Bitfield packing
- `snapshot_delta.h` / `snapshot_chunk.h` — Delta compression

**Interpolation** (`src/net/replication/interp/`):
- `pose_interpolator.h` — Smooth pose interpolation between snapshots
- `snapshot_interp.h` — Snapshot interpolation buffer

### 12.4 Client Networking Runtime

Source: `src/net/client/`, Headers: `include/ferrum/net/client/`

- `runtime_rx.h` — RX/reassembly thread
- `runtime_tx.h` — TX thread for outbound packets

### 12.5 Other Networking

- `ghost_table.h` — Ghost entity tracking (client-side entity proxies)
- `interest.h` — Interest set management
- `packet_header.h` — Packet header format
- `prediction.h` — Client-side prediction state
- `schema_registry.h` — Schema versioning and registration
- `time_sync.h` — Clock synchronization
- `validation.h` — Packet validation
- Test harness: `test_buffer.h`, `test_client.h`, `test_clock.h`, `test_link.h`, `test_transport.h`

### 12.6 Server Networking Runtime

Source: `src/server/`, Headers: `include/ferrum/server/`

- `server/net/runtime.h` — Server network runtime (one fiber per client)
- `server/net/client_fiber.h` — Per-client fiber state
- `server/net/inbound_message.h` — Inbound message queue
- `server/net/state_update_queue.h` — Outbound state update queue
- `server/tick_loop.h` — Server tick loop orchestration
- `server/tick_encoder.h` — Tick state encoding
- `server/repl_server.h` — REPL command server

**Server physics networking:**
- `server/physics/net/body_state_broadcast.h` — Physics state broadcast
- `server/physics/net/priority_body_sender.h` — Priority-based body state sending
- `server/physics/sync/pre_physics_sync.h` — Pre-physics network state sync

**Server entity management:**
- `server/entity.h` — Server entity management
- `server/entity/net/pump.h` — Entity network pump
- `server/player/connection.h` — Player connection state

**Threading requirement:** The networking job system runs on separate OS worker threads from simulation. UDP receive runs on a dedicated OS thread (`net_pump_thread`). Main-thread tick catch-up is capped (default: 3 ticks) to prevent unbounded physics bursts from blocking the network pump.

---

## 13. Mesh Loading

Source: `src/mesh/`, Headers: `include/ferrum/mesh/`

- `obj_loader.h` — Wavefront OBJ mesh file loader

---

## 14. Gameplay Systems

### 14.1 UI (Immediate Mode)
UI is immediate-mode:
- Generates transient meshes each frame
- Avoids retained-state desync problems
- UI runs as jobs producing command buffers for renderer

### 14.2 Inventory
Inventory as ECS components and deterministic transactions:
- Slots arrays / containers
- Stacking/splitting/swapping
- Networking via reliable messages + state hashes

### 14.3 Dialogue System
Dialogue as node/edge graph with condition checks:
- Conditions evaluated against gameplay state (quests, reputation, items)
- Emits UI events (text, choices)
- State stored in ECS components or a "blackboard" resource

### 14.4 Quests
Event-driven DAG; networked event log; snapshots and deltas for quest state.

### 14.5 Flow Field AI
Large-scale movement fields for crowds; integrated with ECS systems and physics constraints.

---

## 15. Platform, Audio, Serialization, Assets, UI, and Services

### 15.1 Platform & Input
- Windowing, input devices (keyboard/mouse/gamepad), timers, filesystem paths
- Poll-driven; integrates with job system; no blocking calls in hot loops

### 15.2 Audio
- Mixer graph with voices/buses/submixes; simple effects; 3D spatialization
- Streaming-friendly buffers; deterministic scheduling via jobs

### 15.3 ECS Serialization & Replay
- Snapshot serialization with versioned schemas; input recording and deterministic replay

### 15.4 Asset Cooking, Hot-Reload & VFS
- Cooked cache with content hashes and dependency graph; VFS mounts and path resolution; thread-safe hot-reload staging and swap

### 15.5 UI, Text & Localization
- Immediate-mode UI; font atlas; basic text shaping; localization tables; deterministic layout

### 15.6 Online Services
- Self-hosted dedicated server model; minimal authentication (session tokens) and simple matchmaking
- Security via server-side validation and rate limiting

---

## Subsystem Summary

| System | Key Source | Key Technologies |
|--------|-----------|------------------|
| Concurrency | `src/job/` | pthread + C11 atomics + user-space fibers, work-stealing deque |
| Memory | `src/memory/` | Thread-safe arena, pool, apool, vm_alloc, Tracy integration |
| ECS | `src/ecs/` | Sparse sets + generation handles, world registry |
| Math | `src/math/` | vec2/3/4, quat, mat4, SIMD-friendly layout |
| Physics | `src/physics/` | Dual TGS/XPBD solver, tiered bodies, 11 joint types, 5 driver types, muscle model, CG solver, spatial hash + static BVH, CCD |
| Animation | `src/animation/` | Ragdoll, IK, constraint solver, fskel format, bone-to-body mapping |
| Rendering | `src/renderer/` | 9-pass pipeline, draw list + radix sort, mesh registry (static/skeletal), shader program, VAO/VBO, UBO, scene graph, gltf loader, GPU skinning, video capture |
| Aegis VM | `src/aegis/` | 256-register bytecode VM, coroutine model, topic pub/sub, async queries, entity snapshot view, assembler |
| Editor | `src/editor/` | Scene editor (SDL2+Clay UI+3D viewport), 50+ commands, undo/redo, mesh modeling (extrude/inset/bevel/CSG/UV), asset registry, TUI interface |
| Networking | `src/net/` | RUDP, reliable/ordered/unreliable channels, delta compression, smallest-3 quat, snapshot interpolation, network emulation |
| Server | `src/server/` | Fiber-per-client, physics broadcast, priority body sender, tick loop, REPL |
| Materials | — | Simplified surface shader, deterministic glTF mapping |
| UI | — | Immediate mode, transient mesh generation |
