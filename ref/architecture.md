# C11-Based High-Performance Game Engine Architecture
## “Ferrum-Engine-C”: A Comprehensive Design Specification

## 1. Architectural Philosophy and Core Design Principles

This report specifies “Ferrum-Engine-C,” a high-performance proprietary game engine built in **pure C11** for AAA-fidelity simulation, **massive co-op multiplayer**, **dynamic world geometry**, and **high-density crowd simulation**.

The defining goals are:

- **Predictable performance:** stable frame times (60/120/144Hz targets), bounded worst-case work.
- **Deterministic behavior where required:** physics and replication logic designed to be replayable and debuggable.
- **Minimal dependencies:** C standard library + platform APIs + OpenGL bindings loaded manually.
- **Data-Oriented Design (DOD):** data layout dominates design decisions to maximize cache coherency.

### 1.1 The Shift from OOP to DOD in C11
Traditional OOP “Actor” hierarchies lead to pointer chasing, cache misses, and polymorphic overhead that scales poorly with thousands of entities. Ferrum-Engine-C uses a strict **Entity Component System (ECS)** to separate:

- **Identity:** entity handles (index + generation)
- **Data:** components stored contiguously
- **Behavior:** systems operating on homogeneous streams of data

This enables high-throughput loops with predictable memory access patterns and straightforward parallelization.

### 1.2 The Concurrency Model: Beyond Standard Threads
Ferrum-Engine-C does not adopt “one OS thread per subsystem.” Instead it uses a **fiber-based job system** inspired by modern AAA engine architectures:

- A small pool of **pinned worker threads**
- Thousands of **user-space fibers** (stackful coroutines)
- Work expressed as fine-grained **jobs**
- Dependencies managed via **counters/events**, not blocking calls

This model minimizes kernel context switches and keeps cores busy even when tasks wait on dependencies.

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

Ferrum-Engine-C uses an **archetype-based ECS** for iteration speed at scale.

### 4.1 Archetype Architecture
Each unique component set forms an archetype table:

- Structure-of-Arrays (SoA) layout: each component type is its own packed column
- entity moves between archetypes when components are added/removed

This increases cost of composition changes but maximizes throughput of component updates (the common case).

### 4.2 Systems and Scheduling
Systems are stateless functions that operate on archetype chunks:

- scheduler builds an execution plan based on declared **read/write sets**
- disjoint systems run concurrently across fibers
- systems can be chunked (e.g., 1k entities per job) for load balancing

### 4.3 Relationship/Parameterization Pattern (C11)
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

---

## 6. Physics and Simulation Architecture (No Third-Party Physics)

The engine implements its own physics subset:

- deterministic fixed timestep (e.g., 60Hz)
- rigid body basics (integrator + constraints as incremental milestones)
- broadphase acceleration (spatial hash / sweep-and-prune)
- narrowphase AABB + simple primitives as baseline

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

---

## 9. Networking Architecture: Massive Co-op

### 9.1 Transport Layer: UDP with Reliability Channels
- Unreliable channel for high-rate state (movement)
- Reliable ordered channel for critical events (inventory, dialogue, terrain edits)

### 9.2 Replication and Delta Compression
- server maintains state history (snapshots)
- client acks last received snapshot
- server sends deltas (bit-packed, quantized)

### 9.3 Client Prediction and Reconciliation
- client predicts local inputs, stores input history
- server sends authoritative state
- client reconciles and re-simulates inputs if error exceeds threshold

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

### 10.3 Dialogue System
Dialogue as node/edge graph with condition checks:

- conditions evaluated against gameplay state (quests, reputation, items)
- emits UI events (text, choices)
- state stored in ECS components or a “blackboard” resource

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
| UI | Immediate mode | transient mesh generation |