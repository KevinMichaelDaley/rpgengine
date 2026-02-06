# Ferrum Engine Architecture Summary
**Date: 2026-02-06**

This document summarizes the current implementation state of the Ferrum Engine, based on code inspection.

---

## Overview

Ferrum is a C11 game engine built around four core pillars:
1. **Fiber-based job system** for cooperative multitasking
2. **Custom memory allocators** (arena, pool, async pool)
3. **Entity Component System** using sparse sets
4. **UDP networking** with reliability channels

The engine uses pure C11 with minimal dependencies (POSIX, OpenGL/GLEW/SDL2).

---

## 1. Job System (`src/job/`, `include/ferrum/job/`)

The job system implements a **fiber-based cooperative scheduler** with work-stealing.

### Core Components

| File | Purpose |
|------|---------|
| `system.h` | Main job system API |
| `fiber.c` | Fiber creation, trampoline, lifecycle |
| `context.h/c` | Platform-specific context switching (x86_64, ARM, AArch64) |
| `dispatch.c` | Job dispatch and yield |
| `queue.c` | Job queue management |
| `ws_deque.h` | Chase–Lev work-stealing deque |
| `counter.c` | Atomic job counters for dependencies |
| `numa.c` | NUMA-aware sharding |

### How It Works

1. **Fiber Creation**: `job_fiber_create()` allocates from `apool_t` (async pool), sets up a stack, and initializes a `job_context_t` with platform-specific registers (callee-saved GPRs, SIMD, FPU state).

2. **Context Switching**: Custom assembly (`job_context_swap`) saves/restores registers directly—no OS kernel involvement. On x86_64, this includes rbx, rbp, r12-r15, rsp, rip, plus fxsave area for FPU/SSE.

3. **Work Stealing**: Each worker owns a `fr_ws_deque_t` (Chase–Lev deque). Workers pop from their own queue; if empty, they steal from other workers' tails. This balances load with minimal contention.

4. **Deterministic Mode**: Single-threaded mode uses a simple queue for reproducible execution order (useful for debugging/replay).

5. **Counters**: `job_counter_t` provides atomic dependency tracking. Jobs wait by yielding until counters reach zero—no blocking syscalls.

### Key APIs
```c
job_system_create(sys, workers, queue_cap, stack_size, fiber_count, deterministic);
job_dispatch(sys, fn, user_data, priority, counter);
job_yield();
job_counter_wait(sys, counter);
```

---

## 2. Memory System (`src/memory/`, `include/ferrum/memory/`)

Three allocator types with explicit ownership:

### Arena Allocator (`arena.h`)
- Linear bump allocator backed by caller-owned buffer
- `arena_alloc()` bumps pointer with alignment
- `arena_reset()` frees all at once
- `arena_mark()/arena_pop_to_mark()` for nested lifetimes
- **Use case**: Frame temporaries, transient allocations

### Pool Allocator (`pool.h`)
- Fixed-size elements with generation counters
- Free list for O(1) alloc/free
- `pool_handle_t` contains index + generation for dangling detection
- **Use case**: Long-lived game objects

### Async Pool (`apool.h`)
- Thread-safe pool variant for job system fiber stacks
- Atomic free list operations
- **Use case**: Fiber stack allocation

---

## 3. Entity Component System (`src/ecs/`, `include/ferrum/ecs/`)

The ECS uses **sparse sets** for component storage—not archetypes (despite architecture.md mentioning archetypes, the code implements sparse sets).

### Components

| Type | Purpose |
|------|---------|
| `entity_t` | Handle with `index` + `generation` |
| `ecs_entity_pool_t` | Entity allocator with generation tracking |
| `ecs_sparse_set_base_t` | Generic component storage |
| `ecs_world_t` | Container for entity pool + registered sets |

### Sparse Set Design
```c
typedef struct ecs_sparse_set_base {
    void *dense;              // Packed component data
    entity_t *dense_entities; // Entity handles for iteration
    uint32_t *sparse;         // Entity index → dense index
    uint32_t capacity;
    uint32_t size;
    size_t stride;
} ecs_sparse_set_base_t;
```

- **Insert**: O(1) append to dense, update sparse lookup
- **Remove**: Swap-and-pop, update sparse indices
- **Lookup**: O(1) via sparse array
- **Iteration**: Linear over dense array (cache-friendly)

### Macro-Generated Type Safety
```c
ECS_DEFINE_SPARSE_SET(position, vec3_t);
// Generates: ecs_sparse_set_position_t, ecs_sparse_set_position_insert(), etc.
```

### Entity Lifecycle
```c
ecs_world_create_entity(world, &entity);   // Allocates from free list, increments generation
ecs_entity_is_alive(pool, entity);         // Validates generation matches
ecs_entity_destroy(pool, entity);          // Increments generation, returns to free list
```

---

## 4. Networking (`src/net/`, `include/ferrum/net/`)

The networking layer implements **reliable UDP** with channel-based message delivery.

### Transport Layers

| Layer | Purpose |
|-------|---------|
| `udp/` | Raw UDP socket abstraction |
| `rudp/` | Reliability, sequencing, ack/nack |
| `channel/` | Reliable/unreliable channel APIs |
| `replication/` | Entity state sync (spawn, transform, interpolation) |
| `topic/` | Pub/sub topic channels |
| `client/` | Client-side RX runtime |

### Reliable Channel (`reliable_channel.h`)
```c
typedef struct net_reliable_channel {
    uint8_t *payloads;           // Slot buffer
    size_t *sizes;
    uint16_t *sequences;
    uint8_t *occupied;
    uint16_t next_send_sequence;
    uint16_t next_receive_sequence;
} net_reliable_channel_t;
```

- Fixed-size slot buffer for pending packets
- Sequence numbers for ordering
- Explicit resend support via `net_reliable_channel_resend()`

### RUDP Stream (`stream.h`)
Higher-level abstraction over reliability:
```c
fr_rudp_stream_t *fr_rudp_stream_create(config);
fr_rudp_stream_push_frame(stream, data, len);  // RX thread pushes frames
fr_rudp_stream_pop(stream, channel, out, &len); // Gameplay pops ordered messages
```

### Replication Messages (`replication/`)
- `spawn.h` / `spawn_batch.h`: Entity spawn messages
- `join.h` / `welcome.h`: Player connection protocol
- `vec3_mm.h`: Quantized position (min/max encoding)
- `quat_snorm16.h`: Quantized rotation (signed normalized 16-bit)
- `input_rot.h`: Input rotation replication
- `interp/`: Pose interpolation for smoothing

### Server-Side Client Fiber (`server/net/client_fiber.h`)
One fiber per connected client:
```c
fr_server_client_fiber_t *fr_server_client_fiber_create(config);
fr_server_client_fiber_inject_frame(fiber, frame, len);  // Process incoming frame
```
- Integrates with topic channels for pub/sub
- Scheduled by job system, not dedicated threads

---

## 5. Renderer (`src/renderer/`, `include/ferrum/renderer/`)

OpenGL 4.6 renderer with AZDO (Approaching Zero Driver Overhead) patterns.

### Core Components

| Component | Purpose |
|-----------|---------|
| `gl_loader.h` | Function pointer table for GL functions |
| `shader_program.h` | Shader compilation/linking |
| `vbo.h` | Vertex buffer objects |
| `vao.h` | Vertex array objects |
| `render_pipeline.h` | Pass-based pipeline execution |
| `bone_palette.h` | Skeletal animation bone matrices |
| `skinning_shader.h` | GPU skinning shader |
| `debug_lines.h` | Debug line rendering |

### Pipeline Architecture
```c
typedef struct render_pass {
    const char *name;
    void (*begin)(void *user_data);
    void (*submit)(void *user_data);
    void (*end)(void *user_data);
    void *user_data;
} render_pass_t;

typedef struct render_pipeline {
    render_pass_t *passes;
    size_t pass_count;
} render_pipeline_t;
```

Default pipeline: Skybox → Forward → Post

### Shader Program
Wraps GL calls with explicit function pointers (no global GL state):
```c
shader_program_create(program, loader, vertex_src, fragment_src, log, log_size);
shader_program_bind(program);
shader_program_handle(program);  // Get raw GL handle
```

### GPU Skinning
- `bone_palette_t`: UBO upload of bone matrices
- Vertex shader applies weighted transforms
- Designed for crowd-scale character rendering

---

## 6. Math (`src/math/`, `include/ferrum/math/`)

SIMD-friendly math types with explicit operations:

| Type | Components |
|------|------------|
| `vec2_t` | x, y |
| `vec3_t` | x, y, z |
| `vec4_t` | x, y, z, w |
| `quat_t` | x, y, z, w (quaternion) |
| `mat4_t` | 4×4 matrix (column-major) |

### Operations
- Basic: add, sub, scale, dot, cross, normalize
- Quaternion: slerp, to/from mat4, angle extraction
- Matrix: multiply, inverse, transpose, look_at, perspective/ortho projection, rotation

---

## 7. Server (`src/server/`)

Server-side subsystems:

| Directory | Purpose |
|-----------|---------|
| `entity/` | Server entity management |
| `net/` | Client fiber networking, state update queue |
| `repl/` | REPL/debug interface |

### State Update Queue (`state_update_queue.h`)
Bridges network RX to simulation:
- Network threads push decoded state updates
- Simulation fibers consume updates each tick

---

## Current Implementation Status

### Implemented (Functional)
- ✅ Fiber-based job system with work stealing
- ✅ Arena, pool, async pool allocators
- ✅ Sparse set ECS with entity generations
- ✅ Reliable/unreliable UDP channels
- ✅ RUDP stream reassembly
- ✅ Entity replication messages (spawn, position, rotation)
- ✅ Basic OpenGL renderer (shaders, VBO/VAO, pipeline)
- ✅ GPU skinning infrastructure
- ✅ Math library (vectors, quaternions, matrices)

### Partially Implemented
- 🔄 Client prediction/reconciliation (messages defined, logic WIP)

### Not Yet Implemented (per architecture.md)
- ❌ Archetype-based ECS (current uses sparse sets)
- ❌ Physics system
- ❌ Terrain/clipmaps
- ❌ AI/behavior trees
- ❌ Audio system
- ❌ UI system
- ❌ Scene/asset streaming

---

## Key Design Patterns

1. **Explicit Ownership**: All APIs document who owns memory and when
2. **Generation Counters**: Handles include generation to detect use-after-free
3. **No Global State**: GL functions passed as pointers, systems take explicit context
4. **Status Codes**: Functions return enums, not exceptions
5. **Platform Abstraction**: Context switching has per-arch implementations
6. **Lock-Free Where Possible**: Atomics for counters, work-stealing deques
7. **Cooperative Scheduling**: `job_yield()` for explicit control transfer
