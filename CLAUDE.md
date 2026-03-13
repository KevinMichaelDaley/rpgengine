## Engine Architecture Summary

Ferrum is a C11 game engine for server-authoritative multiplayer with physics, networking, rendering, and a visual editor. See `ref/` docs for full details.

### Subsystems at a Glance

| Subsystem | Source | Headers | Reference Doc |
|-----------|--------|---------|---------------|
| Physics | `src/physics/` | `include/ferrum/physics/` | `ref/physics.md` |
| Networking | `src/net/`, `src/server/` | `include/ferrum/net/`, `include/ferrum/server/` | `ref/networking_runtime.md`, `ref/server_architecture.md`, `ref/client_architecture.md` |
| Renderer | `src/renderer/` | `include/ferrum/renderer/` | `ref/renderer_spec.md` |
| Editor | `src/editor/` | `include/ferrum/editor/` | `ref/editor_design.md` |
| Job System | `src/job/` | `include/ferrum/job/` | `ref/architecture.md` §3 |
| Memory | `src/memory/` | `include/ferrum/memory/` | `ref/architecture.md` §4 |
| Math | `src/math/` | `include/ferrum/math/` | `ref/architecture.md` §6 |
| Animation | `src/animation/` | `include/ferrum/animation/` | `ref/architecture.md` §8 |
| Aegis VM | `src/aegis/` | `include/ferrum/aegis/` | `ref/aegis_bytecode_spec.md` |
| ECS | `src/ecs/` | `include/ferrum/ecs/` | `ref/architecture.md` §5 |

### Physics Engine
- **Tiered TGS/XPBD solver** with 7 tiers (ANIM + T0-T5) based on distance/importance
- **11 joint types**: distance, ball, hinge, lock, twist, cone-twist, copy-rotation, limit-rotation, limit-position, aim, IK
- **5 joint driver types**: motor, spring, linear actuator, servo, aero-hydraulic
- **Biomechanical muscle system**: Hill force model, tendon dynamics, antagonist pairs
- **Colliders**: sphere, box, capsule, halfspace, mesh, convex (GJK/EPA), compound, point
- **Broadphase**: spatial hash grid + static BVH
- **Parallel physics**: job-based broadphase, narrowphase, constraint solving
- **Island solver** with adaptive iteration scaling and CG coupled solver for ANIM tier
- **CCD** for fast-moving bodies; contact/overlap events; deferred command system

### Renderer
- **OpenGL 3.3+ core profile** with custom `gl_loader_t` (no GLAD/GLEW)
- **9-pass pipeline**: shadow → depth_pre → caster → light_cull → forward → skybox → debug → post → ui
- **Mesh system**: `mesh_registry_t` (handle-based), `static_mesh_t` (per-attribute VBOs), `skeletal_mesh_t`, FVMA binary format
- **Shader system**: `shader_program_t` + `shader_uniform_cache_t` (64-entry cache with type checking)
- **VAO/VBO wrappers**: per-instance GL function pointers, `vao_attribute_t` binding
- **Clay UI backend**: renders Clay commands via GL (text, rectangles, images, borders, scissors)
- **Scene viewport**: FBO-based 3D rendering with Blinn-Phong, grid, entity mesh cache (FVMA loading)
- **Skinning pipeline**: bone palette (SSBO/UBO/TBO), GPU-skinned animation
- **Video capture**: PBO ring + async encoder thread

### Editor
- **Two interfaces**: Scene editor (SDL2/OpenGL/Clay UI) and TUI editor (text-based over TCP)
- **Entity types**: BOX, SPHERE, CAPSULE, MARKER, MESH, HALFSPACE (up to 32)
- **Entity store**: flat array with O(1) freelist, mmap'd backing, `entity_attrs_t` dynamic key-value storage
- **50+ commands**: spawn, delete, move, rotate, scale, clone, save, load, select variants, groups, materials, physics control, mesh commit, etc.
- **Scene editor panels**: viewport (3D FBO), outliner (entity tree), inspector (property editing), TUI (command input)
- **3D viewport**: orbit/pan/zoom camera, grid, entity rendering by type, FVMA mesh loading for MESH entities, selection highlighting
- **Mesh editing**: 60+ modules (extrude, inset, bevel, CSG, UV tools, OBJ import/export, smart unwrap)
- **Physics bridge**: callbacks for spawn/delete/move/mesh_data/joint/material between editor and physics
- **Undo/redo**, selection groups, asset registry, scene sync (offline queue)

### Networking
- **RUDP transport**: reliable UDP with 256-bit ACK window, fragmentation (64-fragment mask), RTT-based retransmit
- **Stream abstraction**: `fr_rudp_stream_t` with push/pop/send/flush per channel
- **Replication**: body state batching (11 per batch), mesh data chunking (440-byte chunks), quantized snapshots
- **Server**: fiber-per-client architecture, entity net pump, priority body sender, dedicated physics thread
- **Client**: 4 threads (main/RX/TX/prediction), ghost table, snapshot interpolation, client-side prediction with replay

### Job System
- Fiber-based (stackful coroutines) on pinned worker threads
- Jobs with counter-based dependencies, work-stealing deques
- Tracy profiling integration with fiber context tracking

### Memory
- Arena allocator (linear bump, thread-safe variant with spinlock)
- Pool allocator for fixed-size objects
- No malloc/free in per-frame code paths — use arenas/pools

### Build
- `make` builds everything; `make test` runs headless tests
- `make TRACY=1` enables profiling; `make TRACY=1 FIBERS=1` adds fiber tracking
- Source auto-discovered via wildcards; tests link against `libheadless.a` (no GL) or `liball.a` (with GL)

---

## Issue Tracking and Task Management

This project uses **tk (ticket)** for issue tracking and task management.
Run `tk prime` for workflow context and setup instructions.

**Quick reference:**
- `tk ready` - Find unblocked work
- `tk create "Title" --type task --priority 2` - Create issue
- `tk close <id>` - Complete work

For full workflow details: `tk prime`
ALWAYS use `tk show` to read the full, extended description of a ticket before trying to implement or continue implementing. 
Explanations of how to do the tasks or why they exist can often be found in other places in ref/ with descriptive filenames; for example, check ref/architecture.md for architectural guidelines. 

# CRITICAL DIRECTIVE (C VERSION)
## Strict Test-Driven Development + Extreme Modularity

### 0. LANGUAGE TARGET
- **Language:** C (C11)
- **Build expectation:** Tests may fail to compile in Phase 1 — this is required.
- **Rule:** No implementation code before tests.

---

## 1. MANDATORY WORKFLOW: TDD & NO SHORTCUTS

### Phase 1: The Test Battery (RED)
Write all unit and regression tests **before** any production `.c/.h` files exist.

- **Happy Path:** expected behavior
- **Edge Cases:** boundaries (0, empty, max, nulls, overflow-adjacent)
- **Failure Modes:** invalid inputs, error propagation

The code is expected not to compile yet.

### Phase 2: Full Implementation (GREEN)
- Implement only what tests require.
- **NO SKELETONS:** no TODOs, no stub returns.
- **NO BACKFILLING:** tests define the API.
- Runtime errors must be handled explicitly (no crashes unless tests demand it).

### Phase 3: Refactor & Verify
- Refactor without changing behavior.
- Re-check all edge and failure cases.
- Enforce file structure rules.

---

## 2. FILE STRUCTURE & GRANULARITY (EXTREME MODULARITY)

### Directory-first Design
Prefer deep hierarchies:
- ✅ `src/physics/collision/broadphase/grid.c`
- ❌ `src/physics/broadphase.c`

### Header / Module Ownership
- One public header per module.
- Public API only in headers.
- Private helpers stay in `.c` files.

### Hard Limits

#### 2-Type Rule (Headers)
A single public header must expose **no more than 2 public types**
(structs, enums, or typedefs).  The exception to this rule is forward declarations; you can use as many forward declarations as are necessary.

#### 4-Function Rule (Source Files)
A single `.c` file must contain **no more than 4 non-static functions**.
Static helpers are allowed but should be minimal.  It is preferable to create a new file than to suppress features or lengthen function bodies to satisfy this rule, especially for unit tests.  

### Module Wiring Rule
Whenever a new module is created:
- Explicitly show the `#include` added to the parent aggregator header.

---

## 3. CODE QUALITY & C IDIOMS

### Error Handling
- No `abort()`, `exit()`, or runtime `assert()` unless tests demand it.
- Prefer `enum` status codes or `bool` + out-parameters.
- All public APIs must document:
  - Ownership rules
  - Nullability
  - Error semantics
  - Side effects
### VLAs are illegal!
Do not use variable-length arrays.  They are extremely unsafe and lead to buffer overruns frequently.  Also, note that our stack sizes are small for any function that runs on a fiber (which could be most functions).  The preferred pattern for functions that manipulate arrays is to perform allocations outside of the array and pass a pointer and a capacity (or a pool/arena) as an argument.  I.e., allocations should be moved as far up the call stack as possible.  Also, never call calloc/malloc/free/other dynamic memory management functions in functions that execute once or more per frame.  Always use pre-allocated heaps, pools, arenas, etc to manage memory that gets rapidly allocated and deallocated.
### Style
- Use variable and function names that are descriptive and indicate their purpose.  Use obvious syntax and avoid
unnecessary brevity when it detracts from readability.  
- Comment code liberally.  Keep comments up-to-date.

### Memory Rules
- Ownership must be explicit.
- No hidden global state unless tests require it.
- Prefer explicit context / allocator objects.

### Safety
- No UB-prone casts.
- No compiler extensions.
- Must be clean under `-Wall -Wextra -Wpedantic`.

### Documentation
All public APIs must be documented using Doxygen-style comments.

---

## 4. DEPENDENCY CONSTRAINTS

### Allowed
- Standard C library headers.
- POSIX headers and libraries (sockets, etc).
- OpenGL, GLEW, and SDL2 (prefer system packages to submodules or including third party code directly in the repo).

### Forbidden
- Third-party libraries unless explicitly whitelisted.
- Hidden framework dependencies.

### Testing
- Use a minimal custom test harness unless otherwise specified.

---

## 5. REQUIRED RESPONSE FORMAT

For every feature request, respond in this exact order:

1. **Test Plan**
   - Happy / Edge / Fail cases

2. **File: `tests/<feature>_tests.c`**
   - Full test code first (may not compile yet)

3. **Plan**
   - List all `.h` and `.c` files to be created
   - Show aggregator `#include` changes

4. **Implementation**
   - Full headers and sources
   - Must satisfy tests and structural rules

5. **Verification**
   - Confirm test coverage
   - Confirm structure and dependency constraints

---

## 6. TRACY PROFILING

This project uses [Tracy](https://github.com/wolfpld/tracy) for real-time profiling.
Tracy is conditionally compiled via `TRACY_ENABLE` and `TRACY_FIBERS`.

### Enabling Tracy

Build with Tracy enabled:
```bash
make TRACY=1          # Basic Tracy zones
make TRACY=1 FIBERS=1 # Tracy + fiber context tracking
```

### Zone Naming Convention

All Tracy zones must follow the hierarchical naming pattern:

```
Subsystem.Stage.DescriptiveParticiple
```

**Examples:**
- `Phys.Solve.IteratingTGS`
- `Phys.Broad.FindingPairs`
- `Net.Repl.EncodingSnapshot`
- `Job.Dispatch.QueuingFiber`

**Rules:**
- Use PascalCase for each segment
- Participle form for the action (e.g., `Building`, `Computing`, `Updating`)
- Keep total length under 40 characters
- Subsystem prefixes: `Phys`, `Net`, `Job`, `Mem`, `Render`, `Audio`, `Game`

### Zone Instrumentation Pattern

```c
#ifdef TRACY_ENABLE
#include "tracy/TracyC.h"
#endif

void phys_stage_broadphase(const phys_broadphase_args_t *args) {
    #ifdef TRACY_ENABLE
    TracyCZoneN(zone, "Phys.Broad.FindingPairs", true);
    #endif

    // ... implementation ...

    #ifdef TRACY_ENABLE
    TracyCZoneEnd(zone);
    #endif
}
```

For parallel jobs with fibers:
```c
#if defined(TRACY_ENABLE) && defined(TRACY_FIBERS)
    TracyCFiberEnter(fiber->tracy_name);
#endif
```

### Budget Files

Reference files for comparing Tracy output against expected budgets:

| File | Purpose |
|------|---------|
| `ref/physics_time_budget.txt` | Function → zone mapping, time % targets |
| `ref/physics_memory_budget.txt` | Struct → memory % targets |
| `ref/physics_performance_analysis.md` | Scaling analysis, bottleneck identification |

### Key Zones to Monitor

**Physics tick (target: 1.5 ms):**
```
Phys.Solve.IteratingTGS      > 600 µs → Island size problem
Phys.Narrow.TestingCollisions > 300 µs → Pair count explosion
Phys.Broad.FindingPairs      > 150 µs → Spatial index tuning needed
Phys.Barrier.*               > 100 µs → Job scheduling overhead
```

**Network (target: 0.5 ms):**
```
Net.Repl.EncodingSnapshot    > 200 µs → Too many changed bodies
Net.Rudp.SendingPackets      > 300 µs → Bandwidth saturation
```

### Memory Tracking

Use Tracy's memory profiling for arena and pool allocations:
```c
#ifdef TRACY_ENABLE
    TracyCAlloc(ptr, size);
    TracyCFree(ptr);
#endif
```

For named memory pools:
```c
#ifdef TRACY_ENABLE
    TracyCAllocN(ptr, size, "phys_body_pool");
#endif
```

### Frame Markers

Mark physics ticks and server frames for timeline correlation:
```c
#ifdef TRACY_ENABLE
    TracyCFrameMarkNamed("PhysTick");
    TracyCFrameMarkNamed("ServerTick");
#endif
```

### Profiling Workflow

1. **Baseline capture:** Run `make bench TRACY=1`, capture 10-second trace
2. **Identify hotspots:** Sort zones by total time, look for > 10% contributors
3. **Check against budget:** Compare zone times to `ref/physics_time_budget.txt`
4. **Investigate anomalies:** Zones exceeding 2× budget indicate problems
5. **Verify fixes:** Re-capture, confirm zone times within budget

### Automated Budget Comparison

The profiling budget files are designed for programmatic comparison:
```bash
# Export Tracy data and compare (future tooling)
tracy-export trace.tracy | scripts/compare_budget.py ref/physics_time_budget.txt
```

### Common Issues

| Symptom | Likely Cause | Check |
|---------|--------------|-------|
| `Phys.Solve.*` spikes | Large islands | Island count in Tracy, look for single huge island |
| `Phys.Narrow.*` spikes | Dense collisions | Pair count, body clustering |
| `Phys.Barrier.*` high | Job imbalance | Worker thread utilization |
| Missing fiber context | `TRACY_FIBERS` not set | Rebuild with `FIBERS=1` |
| Zones not appearing | `TRACY_ENABLE` not set | Verify build flags |

