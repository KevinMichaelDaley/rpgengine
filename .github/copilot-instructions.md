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

