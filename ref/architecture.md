# C11-Based High-Performance Game Engine Architecture
## “Ferrum-Engine-C”: A Comprehensive Design Specification

## 1. Architectural Philosophy and Core Design Principles

This report specifies “Ferrum-Engine-C,” a high-performance proprietary game engine built in **pure C11** for AAA-fidelity simulation, **massive co-op multiplayer**, **dynamic world geometry**, and **high-density crowd simulation**.

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
Traditional OOP “Actor” hierarchies lead to pointer chasing, cache misses, and polymorphic overhead that scales poorly with thousands of entities. Ferrum-Engine-C uses a strict **Entity Component System (ECS)** to separate:

- **Identity:** entity handles (index + generation)
- **Data:** components stored contiguously
- **Behavior:** systems operating on homogeneous streams of data

This enables high-throughput loops with predictable memory access patterns and straightforward parallelization.

### 1.4 Module Index (P_000–P_020)
High-level modules aligned with `ref/prompts.md`:
- P_000 Fiber Runtime & Job System
- P_001 Core Math
- P_002 Memory (Arena + Pool)
- P_003 ECS Core (Sparse Sets + Homogeneous Pools)
- P_004 Renderer Core + Pipeline Graph
- P_005 Geometry Clipmaps (Terrain)
- P_006 Physics (Rigid Bodies, Collisions, Constraints)
- P_007 Networking & Replication (RUDP, Snapshot/Delta, Prediction)
- P_008 Inventory & Modifiers (Networked Transactions)
- P_009 Quest System (Event Log)
- P_010 Dialogue Graph
- P_011 Flow Field AI
- P_012 Scene Management & Asset Streaming (glTF, Lightmaps, Skybox, World Streaming)
- P_013 AI & Scripting (Dynamic Modules, BT, HTN, Hierarchical Pathfinding)
- P_014 Platform & Input Abstraction
- P_015 Audio (Mixer & Spatialization)
- P_016 Render Pipeline & Simplified Surface Shader
- P_017 ECS Serialization, Save-Load & Replay
- P_018 Asset Cooking, Hot-Reload & VFS
- P_019 UI, Text & Localization
- P_020 Online Services (Dedicated Server, Minimal Auth/Matchmaking)

### 1.2 The Concurrency Model: Beyond Standard Threads
Ferrum-Engine-C does not adopt “one OS thread per subsystem.” Instead it uses a **fiber-based job system** inspired by modern AAA engine architectures:

- A small pool of **pinned worker threads**
- Thousands of **user-space fibers** (stackful coroutines)
- Work expressed as fine-grained **jobs**
- Dependencies managed via **counters/events**, not blocking calls

This model minimizes kernel context switches and keeps cores busy even when tasks wait on dependencies.

**Exception: networking and IO boundaries.**
Networking must interact with OS sockets and OS scheduling. On the **client**, the network subsystem runs on dedicated OS thread(s) that do packet IO, reliability/reassembly, and then feed decoded messages into fiber-safe jobs. On the **server**, networking is modeled as one fiber per connected client, scheduled by the job system, with per-client channels bridging to simulation.

### 1.3 Networking as a First-Class Citizen
Massive co-op with client-side prediction and dynamic geometry requires networking to be core architecture, not a bolt-on. The engine uses:

- **Server authoritative simulation**
- **UDP transport with reliability channels**
- **Delta compression and snapshot replication**
- **Chunk-based dynamic geometry propagation** for terrain edits/building

---

## 2. Core Foundation: Fiber-Based Job System

### 2.1 The Fiber Abstraction
A **fiber** is a lightweight execution context with its own stack, cooperatively scheduled atop an OS thread. Context switching is done in user-space by saving/restoring registers and stack pointer.

**Key properties**
- Cooperative (explicit yield), deterministic scheduling possible in debug mode
- Small fixed stacks (e.g., 64KB, configurable), recycled from pools
- No blocking on OS primitives inside fibers; waiting yields to scheduler

### 2.1.1 Implementation Strategy (C11)
The job system initializes **N worker threads** at startup (typically = physical cores or logical cores depending on profiling). Each worker runs:

1. Pop a ready job from its local queue  
2. Execute the job on a fiber  
3. If job waits, fiber yields; worker immediately continues with other work

If its local queue is empty, the worker **steals** jobs from other workers.

### 2.2 Job Scheduling and Dependency Graphs
Each worker owns a local deque. Work stealing takes from another worker’s tail to reduce contention.

**Dependencies**
- **Atomic counters** represent outstanding work.
- Parent job spawns children: increments counter; children decrement on completion.
- Waiting does **not block**: it parks the current fiber until the counter reaches zero.

#### Concurrency model comparison (C11 framing)
- OS threads: expensive switches, blocking waits waste CPU.
- Async/await: great for IO, but CPU-heavy jobs benefit more from fibers.
- Fiber job system: ultra-cheap cooperative switches; excellent for CPU-bound game workloads.

### 2.3 Safety and Failure Containment in C11
C11 provides no compiler-enforced memory safety. Ferrum-Engine-C addresses this with engineering constraints:

- **Explicit ownership contracts** in every API
- **Handles + generation counters** instead of raw pointers in gameplay-facing layers
- **Arena lifetime rules** (frame/level) to prevent accidental long-lived references
- **Failure containment:** jobs return status codes; scheduler isolates failures and can trigger safe shutdown in debug builds

---

## 3. Memory Architecture: Arenas and Deterministic Allocation

### 3.1 Arena Allocation Strategy
Arenas reserve large contiguous blocks and fulfill allocations by bumping a pointer.

#### 3.1.1 Frame Arenas (Bump Allocators)
Per-worker **frame arena**:
- used for transient data (visibility lists, temporary collision pairs, UI vertex buffers)
- reset at end of frame
- alignment-safe allocations

#### 3.1.2 Level Arenas
A **level arena** holds data valid for the duration of a map:
- static terrain meshes
- navigation data
- loaded entity state  
Freed wholesale during map transition.

### 3.2 Integration with ECS
ECS storage is designed to be arena-backed or pool-backed to control fragmentation:

- stable component pools for long-lived components
- dedicated arenas for high-churn subsystems (particles, projectiles)

---

## 4. Entity Component System (ECS) Implementation

Ferrum-Engine-C uses a **hybrid ECS** combining sparse sets for general entities with **homogeneous entity pools** for high-throughput batch processing.

### 4.1 Sparse Set Storage (General Entities)

The core ECS uses sparse sets for flexible component storage:

- **O(1) lookup:** sparse array maps entity index → dense index
- **O(1) add/remove:** swap-and-pop removal, append insertion
- **Cache-friendly iteration:** dense arrays are contiguous
- **Generation counters:** detect use-after-destroy via entity handles

Sparse sets are ideal for heterogeneous entities where components are added/removed dynamically (players, unique objects, interactables).

### 4.2 Homogeneous Entity Pools (Batch Processing)

For compute-intensive entities with fixed component sets, dedicated **pool-per-type** storage provides maximum throughput:

```c
// All NPCs have identical components - store together
typedef struct npc_data {
    vec3_t position;
    vec3_t velocity;
    float health;
    uint32_t ai_state;
} npc_data_t;

pool_t npc_pool;  // 10k NPCs, contiguous memory

// All projectiles have identical components
typedef struct projectile_data {
    vec3_t position;
    vec3_t velocity;
    float lifetime;
    entity_t owner;
} projectile_data_t;

pool_t projectile_pool;  // 50k projectiles, contiguous memory
```

**Advantages:**

- **Perfect cache utilization:** All data for entity type is packed
- **No sparse lookups:** Direct index into pool storage
- **SIMD-friendly:** Can process 4/8 entities per instruction with SoA layout
- **Predictable memory:** Fixed capacity, no fragmentation

**When to use each:**

| Homogeneous Pools | Sparse Sets |
|-------------------|-------------|
| High-volume similar entities (NPCs, projectiles, particles) | Heterogeneous entities (player, unique objects) |
| Hot-path systems (physics, AI batch updates) | Rarely-queried components |
| Fixed component set at spawn | Components added/removed dynamically |
| Thousands+ entities of same type | Dozens of unique entities |

### 4.3 Systems as Pipeline Stages

Systems follow the pure functional transformation model (§1.0.1):

- **Input/output separation:** Systems read from source arrays, write to destination arrays
- **Parallel chunking:** Large pools are split across jobs (e.g., 1k entities per job)
- **Counter-based dependencies:** Stage N+1 waits on Stage N's completion counter

```c
// Example: velocity integration as parallel jobs
void integrate_positions_job(void *user) {
    integrate_args_t *args = user;
    // Read from: args->velocities_in, args->positions_in
    // Write to:  args->positions_out (disjoint from input)
    for (uint32_t i = args->start; i < args->end; ++i) {
        args->positions_out[i] = vec3_add(
            args->positions_in[i],
            vec3_scale(args->velocities_in[i], args->dt)
        );
    }
}

// Dispatch N parallel jobs, wait on counter
job_counter_t done;
job_counter_init(&done, 0);
for (int chunk = 0; chunk < num_chunks; ++chunk) {
    job_dispatch(sys, integrate_positions_job, &args[chunk], 0, &done);
}
job_counter_wait(sys, &done);
// Now positions_out is complete; swap buffers for next frame
```

### 4.4 Relationship/Parameterization Pattern (C11)
Relationships are modeled via components containing **entity handles**, e.g.:

- `target_t { entity_t target; }`
- `parent_t { entity_t parent; }`

Queries can filter by presence of relationship components and validate targets via generation checks.

---

## 5. Rendering Pipeline: Modern OpenGL 4.6 (AZDO)

Rendering targets OpenGL 4.6 features and “Approaching Zero Driver Overhead” patterns:

- Direct State Access (DSA)
- persistent mapped buffers where appropriate
- mult-draw indirect (optional) for large crowds
- careful state change minimization

### 5.0 Render Pipeline Graph (Stages & Passes)
Core renderer exposes a pipeline graph:
- Stages: skybox, forward shading, optional post chain stub.
- Pass ordering deterministic; resources bound via AZDO-friendly layouts.
- ECS/job integration: pipeline execution jobs with explicit dependencies.

### 5.0.1 Simplified Surface Shader (Non-PBR)
Materials use a minimal parameter set:
- `base_color`, `roughness_like`, `spec_like`, `emissive`, optional `normal_map`.
- glTF metallic-roughness mapped deterministically (e.g., `roughness_like = roughness`, `spec_like = 1 - metallic`).
- Mapping is lossy but stable; documented for reproducibility.

### 5.1 Direct State Access (DSA) and Bindless (Optional)
- DSA removes bind-to-edit patterns and reduces global state coupling.
- Bindless textures can remove texture binding limits (if supported), but must be optional and capability-checked.

### 5.2 Clustered Forward Shading
Clustered forward shading supports thousands of dynamic lights:

- compute step generates cluster bounds
- compute culls lights into per-cluster lists (SSBO)
- forward pass shades using only relevant lights per pixel

### 5.3 Static Lightmapping (Hybrid)
Static geometry uses baked lightmaps; dynamic lights add on top through clustered shading.

### 5.4 GPU Geometry Clipmaps for Terrain
Clipmaps provide constant geometry memory usage:

- nested grids centered on camera
- toroidal updates (offset snapping)
- height sampled in vertex shader
- optional streaming of height/weight maps via PBOs (asynchronous)

### 5.5 Asset Streaming and PBO Pipeline
A dedicated IO thread:
- reads & decompresses assets into CPU buffers
- stages into PBOs or persistently mapped buffers
- render thread issues GPU uploads without stalls

### 5.6 Scene Management & World Streaming (P_012)
- World partitioned into streaming regions/chunks with dependency metadata.
- Seamless transitions via prefetch/unload policies; cross-scene handoff preserves entity IDs/state.
- glTF 2.0 import: meshes, skeletons/skins, materials mapped to simplified surface shader; KTX2/PNG textures, sRGB handling.
- Lightmaps: UV2 generation and baking pipeline; lightmap volumes/zones integrate with dynamic clustered lighting.
- Skybox integrated as a pipeline stage; asset streaming aligns with networking interest sets.

---

## 6. Physics and Simulation Architecture (No Third-Party Physics)

The engine implements its own physics subset:

- deterministic fixed timestep (e.g., 60Hz)
- rigid body basics (integrator + constraints as incremental milestones)
- broadphase acceleration (spatial hash / sweep-and-prune)
- narrowphase AABB + simple primitives as baseline

### 6.1 Rigid Bodies & Constraints (P_006)
- Integrator: semi-implicit Euler; fixed timestep; sleeping/wake based on thresholds.
- Broadphase: spatial hash; optional sweep-and-prune.
- Narrowphase: contacts/manifolds for primitives; restitution/friction coefficients.
- Solver: sequential impulses; stable stacking; warm starting optional.
- Constraints: distance, ball-and-socket, hinge (limits/motor); articulated bodies supported.
- Sensors/ray tests: overlap sets; force fields; non-solid interactions.

Synchronization:
- physics reads transforms → simulates → writes back transforms
- rendering interpolates between previous/current physics states for smooth visuals

Force fields/sensors:
- sensor volumes produce overlap sets, apply forces without collision response

---

## 7. Animation System

### 7.1 Data Pipeline
- keyframed skeletal animation: translation/rotation/scale curves
- rotations interpolated with quaternion SLERP, others with LERP
- blending between multiple clips

### 7.2 GPU Skinning
GPU skinning is required for crowd scale:

- bone palette uploaded via UBO/SSBO/TBO (capability-based)
- vertex shader applies weighted bone transforms

### 7.3 Clip Import & Evaluation (P_012)
- Import clips (translation/rotation/scale) and retarget to skeletons.
- CPU jobs evaluate keyframes and produce bone palettes uploaded per-frame.
- Networking: `animation_state` schema replicated for consistent animation across clients.

---

## 8. AI and Crowd Simulation

### 8.1 Proximity-Based Scheduler (AI LOD)
AI updates are LOD’d based on distance to players:

- spatial hash for neighbor queries
- LOD buckets:
  - near: update every frame
  - mid: update every N frames
  - far: sparse updates + simplified movement/animation

### 8.2 Utility AI + Behavior Trees (Data-Oriented)
- utility scores decide goals
- behavior tree executes goal steps
- implemented as ECS systems operating over packed arrays, scheduled via fibers

### 8.3 Scripting, HTN, and Hierarchical Pathfinding (P_013)
- Dynamic modules via `dlopen` with versioned ABI; manifest-driven attribute registration.
- Behavior Trees and HTN planning integrated; hierarchical pathfinding (zones→cells) for large worlds.
- Networking: `ai_state` and `custom_attributes` replicated; server authoritative decisions.

---

## 9. Networking Architecture: Massive Co-op

This section describes the *runtime shape* of networking: which parts run on OS threads, which run on fibers, and what other subsystems are allowed to see.

For a more detailed description of the runtime message flow and channel abstractions, see `ref/networking_runtime.md`.

### 9.1 Transport Layer: UDP with Reliability Channels
- Unreliable channel for high-rate state (movement)
- Reliable ordered channel for critical events (inventory, dialogue, terrain edits)

**Important layering rule:** retransmission, reordering, and packet reconstruction happen *above* the protocol/frame parsing layer and *before* any other subsystem reads messages. Subsystems should not parse RUDP frames; they should only consume an abstracted per-channel **reliable UDP stream**.

### 9.2 Client Networking Runtime (Threaded IO + Job Dispatch)
On the client, networking runs on its own OS thread(s):

- **RX / reassembly thread:** receives UDP packets, reorders/reassembles reliable streams per channel, and pumps decoded messages into subscribed jobs.
- **TX thread:** polls outbound channels for messages to send and emits UDP packets.

**Channel abstraction:** subscriptions are to a topic/channel that behaves like a socket backed by a long ring buffer. The network threads are the only writers/readers of the OS socket; gameplay subsystems only see channel streams.

**Subsystem boundary:** modules like spawned-entity management run on the job system (fiber-safe) and are fed an arena (with a pool-backed backing store) for allocating entities created by server commands.

### 9.3 Server Networking Runtime (One Fiber per Client)
On the server, the network subsystem uses **one fiber per client** scheduled by a **dedicated networking job system**:

- Each client fiber maintains at least two fiber-local channels: **reliable** and **unreliable** (e.g., physics/motion updates).
- Client fibers publish decoded inbound messages into a **global inbound topic/queue** consumed by simulation/gameplay jobs on other fibers.
- Outbound simulation results are published into per-client outbound channels for the client fibers to serialize and send.

**Threading requirement:** client fibers run on a **separate `job_system_t`** with dedicated OS worker threads (typically 1, configurable via CLI). This ensures that physics tick latency spikes cannot starve client fiber scheduling. The main-thread tick catch-up loop is capped (default: 3 ticks) to prevent unbounded physics-only bursts from blocking the network pump.

**Important distinction:** not all spawnable entities represent players.

- Many entities (NPCs, props, projectiles) can spawn and replicate without any corresponding “join”.
- Some player-like entities may join and exist, but should not necessarily spawn to every remote client (e.g., invisible players, far-away players outside interest).

To avoid conflating these concepts, the server treats “player connectivity” as its own data model.

**`player_connection_t` (conceptual):**
- `player_id` (stable id for the connected player)
- `world_pos` (plain `vec3` data stored by value, used for interest decisions)
- `player_should_spawn_remote` (flag used to decide whether to emit player-spawn messages to a given remote client)

The entity/world simulation uses this connection data to drive networking events, rather than assuming a 1:1 mapping between “connected client” and “spawned entity”.

**Event taxonomy (conceptual):**
- `EVT_PLAYER_JOIN`: a player connection is established and authorized.
- `EVT_PLAYER_SPAWN`: the player’s in-world representation should spawn for some remote client(s) based on interest/visibility rules.
- `EVT_ENTITY_JOIN`: reserved for non-player “remote entity processes” (e.g., audit/observer/bridge clients) that join but are not players.
- `EVT_ENTITY_SPAWN`: non-player entity spawn events (NPCs/props/etc).

**Threading requirement:** allocate at least **one dedicated OS worker thread** in the networking job system for client fiber progress. For higher client counts, increase to 2+. The simulation job system uses separate workers for physics, skinning, and gameplay jobs.

### 9.2 Replication and Delta Compression
- server maintains state history (snapshots)
- client acks last received snapshot
- server sends deltas (bit-packed, quantized)

### 9.3 Client Prediction and Reconciliation
- client predicts local inputs, stores input history
- server sends authoritative state
- client reconciles and re-simulates inputs if error exceeds threshold

### 9.5 Interest Management & Time Sync (P_007)
- Interest sets derive from streamed regions (P_012); clients receive only in-range entities.
- Deterministic time sync; server broadcasts clock; clients adjust drift and apply interpolation windows.

### 9.6 Networking Aggregator: Schemas & Channel Policies
- Standardized schemas: `transform`, `rigid_body_correction`, `inventory_container`, `quest_log`, `dialogue_state`, `animation_state`, `ai_state`, `custom_attributes`.
- Policies: quantization, reliability channel selection, priority tiers; versioning and manifest registration for custom attributes.
- Headers: sequence/ack/ack_bits; baselines and snapshot-deltas maintained per-entity.

### 9.4 Dynamic Level Geometry Propagation
Terrain/geometry edits replicated as commands:

- world divided into chunks with monotonic version numbers
- edits broadcast as “brush commands” (op, pos, radius, strength)
- late joiners request full chunk state (compressed), not full edit history

---

## 10. Gameplay Systems

### 10.1 UI (Immediate Mode)
UI is immediate-mode:

- generates transient meshes each frame
- avoids retained-state desync problems
- UI runs as jobs producing command buffers for renderer

### 10.2 Inventory
Inventory as ECS components and deterministic transactions:

- slots arrays / containers
- stacking/splitting/swapping
- networking via reliable messages + state hashes

### 10.2.1 Transactions & Idempotency (P_008)
- Server-authoritative transactions with event logs and state hashes; idempotent retries.
- Snapshots/deltas for container state; reconciliation resolves conflicts deterministically.

### 10.3 Dialogue System
Dialogue as node/edge graph with condition checks:

- conditions evaluated against gameplay state (quests, reputation, items)
- emits UI events (text, choices)
- state stored in ECS components or a “blackboard” resource

### 10.4 Quests (P_009)
- Event-driven DAG; networked event log; snapshots and deltas for quest state.
- Server authoritative; client UI reflects replicated progression.

### 10.5 Flow Field AI (P_011)
- Large-scale movement fields for crowds; integrated with ECS systems and physics constraints.

---

## 11. Conclusion

Ferrum-Engine-C is a cohesive, performance-first architecture built on:

- a **fiber-based job system**
- **predictable memory** via arenas/pools
- **archetype ECS** for high-throughput iteration
- **modern OpenGL 4.6 AZDO** patterns
- **server-authoritative networking** with prediction, delta compression, and dynamic geometry propagation

This design trades third-party convenience for total control over performance characteristics, debuggability, and long-term maintainability under strict constraints.

---

## 12. Platform, Audio, Serialization, Assets, UI, and Services

### 12.1 Platform & Input (P_014)
- Windowing, input devices (keyboard/mouse/gamepad), timers, filesystem paths; event + snapshot APIs; sandboxed asset paths.
- Poll-driven; integrates with job system; no blocking calls in hot loops.

### 12.2 Audio (P_015)
- Mixer graph with voices/buses/submixes; simple effects; 3D spatialization and attenuation.
- Streaming-friendly buffers; deterministic scheduling via jobs; ECS audio components.

### 12.3 ECS Serialization & Replay (P_017)
- Snapshot serialization with versioned schemas (reuse networking where possible); input recording and deterministic replay.

### 12.4 Asset Cooking, Hot-Reload & VFS (P_018)
- Cooked cache with content hashes and dependency graph; VFS mounts and path resolution; thread-safe hot-reload staging and swap.

### 12.5 UI, Text & Localization (P_019)
- Immediate-mode UI; font atlas; basic text shaping; localization tables; deterministic layout.

### 12.6 Online Services (P_020)
- Self-hosted dedicated server model; minimal authentication (session tokens) and simple matchmaking (lobbies/direct join).
- Security via server-side validation and rate limiting; explicitly no client-side anti-cheat.

---

## Subsystem Summary (C11)

| System | Strategy | Key Technologies |
|---|---|---|
| Concurrency | Fiber job system | C11 threads/atomics + user-space fibers |
| Memory | Frame/Level arenas + pools | custom allocators |
| ECS | Archetype-based SoA | handles + generation |
| Rendering | OpenGL 4.6 AZDO, clustered forward | DSA, SSBO/UBO, compute shaders |
| Terrain | GPU geometry clipmaps | toroidal updates, optional PBO streaming |
| Lighting | Hybrid baked + clustered | light lists per cluster |
| Physics | Deterministic rigid body baseline | spatial hash broadphase, fixed tick |
| AI | Utility + BT + LOD scheduler | spatial hash, chunked jobs |
| Networking | UDP + reliable channels | snapshot delta, prediction/reconciliation |
| Scene/Assets | Streaming, glTF import, UV2/lightmaps, skybox | world streaming, interest-aligned |
| Materials | Simplified surface shader | deterministic glTF→shader mapping |
| Inventory/Quests | Server-authoritative transactions/logs | snapshots/deltas, idempotency |
| AI & Scripting | Dynamic modules, BT/HTN/pathfinding | `ai_state`/`custom_attributes` replication |
| Platform | Input/window/timers/fs | poll + snapshot APIs |
| Audio | Mixer/spatialization | streaming buffers |
| Serialization | ECS snapshots + replay | versioned schemas |
| Assets | Cooking/VFS/hot-reload | hash cache, staging swap |
| UI/Text | Immediate-mode + localization | font atlas, shaping |
| Services | Dedicated server | minimal auth/matchmaking, server validation |
| UI | Immediate mode | transient mesh generation |