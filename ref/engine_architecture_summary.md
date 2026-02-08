# Engine Architecture Summary

This document provides a unified view of how the three core subsystems of the
Ferrum engine connect: the **Job/Memory System**, the **Networking Runtime**,
and the **Physics Simulation**. It shows the data flow between subsystems, the
threading model, and the memory ownership boundaries.

See the detailed callgraphs for per-stage breakdowns:
- `ref/job_memory_callgraph.md` — Fiber lifecycle, work-stealing, allocators
- `ref/networking_callgraph.md` — RUDP, topic channels, replication
- `ref/physics_tick_callgraph.md` — 13-stage physics pipeline

---

## Legend

```
┌─────────────────────────────────────────────────────────────────────────────┐
│ LEGEND                                                                      │
├─────────────────────────────────────────────────────────────────────────────┤
│ ═══════  Subsystem boundary                                                 │
│ ───────  Synchronous call                                                   │
│ ·······  Async job dispatch                                                 │
│ ┄┄┄┄┄┄┄  Data flow between subsystems                                      │
│ ──────▶  Control flow                                                       │
│ ══════▶  Data dependency                                                    │
│ ◆        Sync barrier                                                       │
│ ●        Backpressure boundary                                              │
│                                                                             │
│ [JOB]    Runs on job system fiber                                           │
│ [THREAD] Runs on dedicated OS thread                                        │
│ [MAIN]   Runs on main/server loop thread                                    │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## Subsystem Architecture

```
═══════════════════════════════════════════════════════════════════════════════
                        FERRUM ENGINE — SUBSYSTEM MAP
═══════════════════════════════════════════════════════════════════════════════

┌─────────────────────────────────────────────────────────────────────────────┐
│                          APPLICATION LAYER                                  │
│                                                                             │
│  Server Main Loop              Client Main Loop                            │
│  ├── Network pump              ├── Render frame                            │
│  ├── Physics tick               ├── Network RX poll                         │
│  ├── Simulation update         ├── Input handling                          │
│  └── Outbound replication      └── Client TX                               │
└───────────┬─────────────────────────────────┬───────────────────────────────┘
            │                                 │
            ▼                                 ▼
┌───────────────────────┐  ┌────────────────────────────┐  ┌─────────────────┐
│   PHYSICS SYSTEM      │  │    NETWORKING SYSTEM        │  │ ECS + RENDERER  │
│                       │  │                            │  │                 │
│ phys_world_t          │  │ Server:                    │  │ ecs_world_t     │
│ ├── body_pool         │  │ ├── net_runtime            │  │ ├── entity_pool │
│ ├── collider_pool     │  │ │   ├── client_fibers[]   │  │ ├── sparse_sets │
│ ├── spatial_grid      │  │ │   ├── inbound_topic     │  │ └── components  │
│ ├── manifold_cache    │  │ │   └── out_topics[][]    │  │                 │
│ └── frame_arena       │  │ ├── state_update_queue    │  │ Skinning:       │
│                       │  │ └── topic_dispatcher      │  │ ├── pipeline    │
│ 13 pipeline stages    │  │                            │  │ ├── skeletons   │
│ (see callgraph)       │  │ Client:                    │  │ └── bone_palette│
│                       │  │ ├── client_rx              │  │                 │
│ Tiered simulation:    │  │ ├── rudp_peer              │  │ Renderer:       │
│ T0..T5 distance-based │  │ └── topic_channels         │  │ ├── pipeline    │
│                       │  │                            │  │ ├── shader_prog │
│ Constraint special:   │  │ Replication:               │  │ ├── vbo/vao     │
│ Planar/Sphere/Generic │  │ ├── schema_registry        │  │ └── gl_loader   │
│                       │  │ ├── quantization           │  │                 │
└───────────┬───────────┘  │ └── spawn/state_cube/etc.  │  └────────┬────────┘
            │              └────────────┬───────────────┘           │
            │                           │                           │
            ▼                           ▼                           ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                       JOB / MEMORY SYSTEM                                   │
│                                                                             │
│  job_system_t                    Memory Allocators                          │
│  ├── workers[N]                  ├── arena_t     (linear, per-frame)       │
│  ├── ws_deques[N]                ├── pool_t      (generation, single-thrd) │
│  ├── fiber_stack_pool (apool_t)  └── apool_t     (lock-free, concurrent)   │
│  ├── job_counter_t (sync)                                                   │
│  └── context_swap (asm)          Platform: x86-64 fiber context + FXSAVE   │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## Threading Model

```
═══════════════════════════════════════════════════════════════════════════════
                          THREAD ALLOCATION
═══════════════════════════════════════════════════════════════════════════════

Server Process:
┌─────────────────────────────────────────────────────────────────────────────┐
│                                                                             │
│  Thread 0 [MAIN]           Server main loop                                │
│  ├── fr_server_net_runtime_pump()      Demux inbound UDP                   │
│  ├── phys_world_tick()                 Dispatches physics jobs              │
│  ├── simulation_update()              Game logic (ECS queries)             │
│  └── outbound_replication()           Push to per-client out topics        │
│      ├── Sleeping bodies skipped (zero bandwidth)                         │
│      ├── Top 16 fastest awake bodies → reliable topic                     │
│      └── Remaining awake bodies → unreliable topic                        │
│                                                                             │
│  Thread 1..N [WORKER]      Job system workers                              │
│  ├── Physics stage jobs    (broadphase, narrowphase, solver chunks)        │
│  ├── Client fiber jobs     (per-client RUDP RX/TX processing)             │
│  ├── Skinning evaluation   (skeleton joint computation)                   │
│  └── Simulation jobs       (gameplay handlers via topic dispatch)          │
│                                                                             │
│  Thread N+1 [TOPIC PUMP]   Topic dispatcher background thread             │
│  └── Polls topics → dispatches handler jobs                               │
│                                                                             │
│  MINIMUM: 3 threads (main + 2 workers for client fiber progress)          │
│  RECOMMENDED: main + (num_cores - 1) workers                              │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘

Client Process:
┌─────────────────────────────────────────────────────────────────────────────┐
│                                                                             │
│  Thread 0 [MAIN]           Client main loop                                │
│  ├── SDL_PollEvent()                 Input handling                        │
│  ├── render_pipeline_execute()       Draw calls                            │
│  ├── net_rudp_peer_send_*()          TX to server                          │
│  └── fr_topic_channel_pop()          Consume decoded server messages       │
│                                                                             │
│  Thread 1 [RX]             Client receive thread                           │
│  ├── net_udp_socket_recvfrom()       Receive UDP datagrams                │
│  ├── net_reliable_channel_*()        Reassembly                           │
│  └── fr_topic_channel_push()         Push decoded messages to topics      │
│                                                                             │
│  Thread 2..M [WORKER]      Job system workers (optional)                   │
│  ├── Skinning pipeline jobs                                                │
│  └── Client-side prediction jobs (future)                                 │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## Cross-Subsystem Data Flows

```
═══════════════════════════════════════════════════════════════════════════════
                    SERVER TICK — FULL DATA FLOW
═══════════════════════════════════════════════════════════════════════════════

  ┌─── NETWORK RX ──────────────────────────────────────────────────────────┐
  │                                                                         │
  │  UDP Socket                                                             │
  │  ├── recvfrom() → raw datagram                                         │
  │  ├── wire_decode() → header + frame                                    │
  │  ├── route → client_fiber[client_id]                                   │
  │  ├── stream_push_frame() → reliable channel buffer                     │
  │  ├── channel_receive() → in-order message                              │
  │  └── topic_push(inbound_topic) → ring buffer         ●                │
  │                                                       │                 │
  └───────────────────────────────────────────────────────┼─────────────────┘
                                                          │
                                                          ▼
  ┌─── SIMULATION ──────────────────────────────────────────────────────────┐
  │                                                                         │
  │  Topic Dispatcher / State Update Queue                                  │
  │  ├── topic_pop() or queue_pop() → decoded player input                 │
  │  ├── Apply to ECS world (entity transforms, input state)               │
  │  │   └── ecs_sparse_set_*_insert/get()                                │
  │  │                                                                     │
  │  ├── Physics Tick (13 stages):                   [JOB dispatch]        │
  │  │   ├── Stage 0: Step plan                                            │
  │  │   ├── Stage 1: Tier classify (distance + occlusion)                │
  │  │   ├── Stage 2: Spatial grid update                                  │
  │  │   ├── Stage 3: AABB compute                                        │
  │  │   ├── Stage 4: Broadphase (grid-accelerated)                       │
  │  │   ├── Stage 5: Merge + warmstart (manifold cache)                  │
  │  │   ├── Stage 6: Narrowphase (sphere simplification at distance)     │
  │  │   ├── Stage 7: Manifold merge                                      │
  │  │   ├── Stage 8: Stability analysis                                  │
  │  │   ├── Stage 9: Constraint build + specialization                   │
  │  │   ├── Stage 10: Island detection                                    │
  │  │   ├── Stage 11: Solve (TGS T0-T1 / XPBD T2-T4)                   │
  │  │   ├── Stage 12: Integration                                         │
  │  │   └── Stage 13: Buffer swap + events                               │
  │  │                                                                     │
  │  │   Memory: frame_arena (per-tick), body_pool (persistent)           │
  │  │   Jobs: fork-join per stage, counter-based barriers                │
  │  │                                                                     │
  │  └── Output: updated entity transforms + impact events                │
  │                                                                         │
  └──────────────────────────────────────────────────────┬──────────────────┘
                                                         │
                                                         ▼
  ┌─── NETWORK TX ──────────────────────────────────────────────────────────┐
  │                                                                         │
  │  Replication Encode                                                     │
  │  ├── For each entity visible to client:                                │
  │  │   ├── quantize_vec3_mm(pos)                                        │
  │  │   ├── quantize_quat_snorm16(rot)                                   │
  │  │   ├── state_cube_encode(&msg, payload, size)                       │
  │  │   └── topic_push(out_unreliable[client_id])        ●               │
  │  │                                                                     │
  │  ├── For spawns/events:                                                │
  │  │   ├── spawn_encode / welcome_encode                                │
  │  │   └── topic_push(out_reliable[client_id])          ●               │
  │  │                                                                     │
  │  └── Per-client fiber TX:                              [JOB]          │
  │      ├── topic_pop(out_reliable) → rudp_send_reliable_via()           │
  │      ├── topic_pop(out_unreliable) → rudp_send_unreliable_via()       │
  │      └── rudp_peer_tick_resend_via() (retransmit)                     │
  │                                                                         │
  └─────────────────────────────────────────────────────────────────────────┘

═══════════════════════════════════════════════════════════════════════════════
                    CLIENT FRAME — FULL DATA FLOW
═══════════════════════════════════════════════════════════════════════════════

  ┌─── NETWORK RX (background thread) ─────────────────────────────────────┐
  │                                                                         │
  │  UDP recvfrom() → parse frame → channel_send_sequence()                │
  │  └── channel_receive() → topic_push()              ●                   │
  │                                                                         │
  └────────────────────────────────────────────────────┬────────────────────┘
                                                       │
                                                       ▼
  ┌─── GAMEPLAY / RENDER ──────────────────────────────────────────────────┐
  │                                                                         │
  │  Consume Messages:                                                     │
  │  ├── topic_pop(spawn_topic) → create entity in ECS                    │
  │  │   ├── ecs_world_create_entity()                                    │
  │  │   ├── dequantize_vec3_mm(pos_mm) → vec3_t                         │
  │  │   └── ecs_sparse_set_*_insert()                                    │
  │  │                                                                     │
  │  ├── topic_pop(state_topic) → update entity transform                 │
  │  │   ├── dequantize_vec3_mm(pos_mm)                                   │
  │  │   ├── dequantize_quat_snorm16(rot)                                 │
  │  │   └── interpolate / predict (pose_interpolator_t)                  │
  │  │                                                                     │
  │  Render:                                                               │
  │  ├── skinning_pipeline_update(job_system, skeleton_set, skin_set)     │
  │  │   └── job_dispatch() × N skeleton evaluation jobs                  │
  │  ├── skinning_pipeline_build_draw_list()                              │
  │  ├── For each drawable:                                                │
  │  │   ├── bone_palette_buffer_update() + bind()                        │
  │  │   └── glDrawArrays() / glDrawElements()                           │
  │  └── render_pipeline_execute()                                         │
  │                                                                         │
  │  Send Input:                                                           │
  │  ├── input_rot_encode(&msg, payload, size)                            │
  │  ├── rudp_peer_send_reliable() → UDP sendto()                         │
  │  └── rudp_peer_tick_resend()                                          │
  │                                                                         │
  └─────────────────────────────────────────────────────────────────────────┘
```

---

## Memory Ownership Map

```
═══════════════════════════════════════════════════════════════════════════════
                    MEMORY OWNERSHIP BY SUBSYSTEM
═══════════════════════════════════════════════════════════════════════════════

JOB SYSTEM owns:
├── fiber_stack_pool: apool_t
│   └── Each slot: job_fiber_t + stack_bytes
│   └── Lock-free alloc/free across all worker threads
├── ws_deques[]: fr_ws_deque_t (per-worker)
├── inject_ring[]: job_fiber_t*
├── workers[]: thrd_t
└── Sync primitives: queue_lock, queue_cond

PHYSICS SYSTEM owns:
├── body_pool: pool_t (persistent across ticks)
│   └── bodies_curr / bodies_next (double-buffered)
├── collider_pool: pool_t (shape references)
├── sphere_pool, box_pool, capsule_pool: pool_t (shape data)
├── spatial_grid: phys_spatial_grid_t (persistent hash grid)
├── manifold_cache: phys_manifold_cache_t (warmstart data)
└── frame_arena: arena_t (per-tick, reset every tick)
    └── All transient: tier_lists, collision_pairs, contacts,
        manifolds, constraints, islands, velocity deltas

NETWORKING (Server) owns:
├── net_runtime: fr_server_net_runtime_t
│   ├── client_fibers[max_clients]: fr_server_client_fiber_t*
│   │   └── Each fiber owns: fr_rudp_stream_t → reliable_channel_t[]
│   ├── out_reliable[max_clients]: fr_topic_channel_t*
│   └── out_unreliable[max_clients]: fr_topic_channel_t*
├── inbound_topic: fr_topic_channel_t (borrowed from app)
├── state_update_queue: fr_state_update_queue_t (ring buffer)
└── Per-peer: net_rudp_peer_t (send_slots[], reasm_buf[])

NETWORKING (Client) owns:
├── client_rx: fr_client_rx_t
│   ├── channels[max_channels]: net_reliable_channel_t
│   └── rx_thread: thrd_t
├── rudp_peer: net_rudp_peer_t (send_slots for retransmit)
└── topic_channels[]: fr_topic_channel_t*

ECS owns:
├── entity_pool: ecs_entity_pool_t (generation-based)
└── sparse_sets[]: ecs_sparse_set_base_t (per component type)
    └── Dense arrays: cache-coherent component storage

RENDERER owns:
├── shader_program_t (GL handles + function pointers)
├── vbo_t, vao_t (vertex data)
├── bone_palette_buffer_t (GPU skinning UBO/SSBO/TBO)
└── skinning_pipeline_t (CPU-side skeleton evaluation)
    └── palette_matrices[] (per-entity bone matrices)

═══════════════════════════════════════════════════════════════════════════════
                    ALLOCATION STRATEGY PER SUBSYSTEM
═══════════════════════════════════════════════════════════════════════════════

┌─────────────┬───────────────────────┬────────────────────────────┐
│ Subsystem   │ Allocator             │ Pattern                    │
├─────────────┼───────────────────────┼────────────────────────────┤
│ Job system  │ apool_t (fibers)      │ Lock-free alloc/free       │
│             │ heap (queues, threads)│ One-time at creation       │
├─────────────┼───────────────────────┼────────────────────────────┤
│ Physics     │ pool_t (bodies, etc.) │ Persistent game objects    │
│             │ arena_t (per-tick)    │ Batch alloc, reset each    │
│             │                       │ tick. ~76 MB frame arena   │
├─────────────┼───────────────────────┼────────────────────────────┤
│ Networking  │ heap (ring buffers,   │ Allocated at runtime       │
│             │   channels, peers)    │ creation, freed on destroy │
│             │ topic_channel_t rings │ Fixed-capacity ring bufs   │
├─────────────┼───────────────────────┼────────────────────────────┤
│ ECS         │ heap (sparse arrays,  │ Grow-on-demand for         │
│             │   dense arrays)       │ component registration     │
├─────────────┼───────────────────────┼────────────────────────────┤
│ Renderer    │ GL driver (GPU)       │ VBOs, textures, SSBOs      │
│             │ heap (CPU-side)       │ Skinning pipeline buffers  │
└─────────────┴───────────────────────┴────────────────────────────┘
```

---

## Subsystem Dependency Graph

```
═══════════════════════════════════════════════════════════════════════════════
                    BUILD / INIT DEPENDENCY ORDER
═══════════════════════════════════════════════════════════════════════════════

                    ┌─────────────────────┐
                    │   Memory Allocators  │
                    │ arena_t, pool_t,     │
                    │ apool_t              │
                    └─────────┬───────────┘
                              │
                    ┌─────────▼───────────┐
                    │   Job System         │
                    │ job_system_t         │
                    │ (uses apool_t for    │
                    │  fiber stacks)       │
                    └───┬─────────┬───────┘
                        │         │
           ┌────────────▼──┐   ┌──▼────────────────┐
           │  Physics      │   │  Networking        │
           │ phys_world_t  │   │ net_runtime_t      │
           │ (uses pool_t, │   │ (uses job_system_t │
           │  arena_t,     │   │  for client fibers,│
           │  job_system_t │   │  topic_channel_t   │
           │  for parallel │   │  for messages)     │
           │  stages)      │   │                    │
           └───────┬───────┘   └──┬─────────────────┘
                   │              │
                   ▼              ▼
           ┌──────────────────────────────┐
           │  ECS World                    │
           │ ecs_world_t                   │
           │ (physics writes transforms,   │
           │  networking spawns entities,   │
           │  renderer reads components)    │
           └──────────────┬────────────────┘
                          │
                ┌─────────▼──────────┐
                │  Renderer           │
                │ render_pipeline_t   │
                │ skinning_pipeline_t │
                │ (reads ECS data,    │
                │  uses job_system_t  │
                │  for skinning jobs) │
                └─────────────────────┘

Init order:
  1. Memory allocators (arena_init, pool_init)
  2. Job system (job_system_create, job_system_start)
  3. Networking (net_runtime_create) + Physics (phys_world_create)
  4. ECS world (ecs_world_create, register components)
  5. Renderer (GL init, shader compile, pipeline setup)

Shutdown order: reverse of init
  5 → 4 → 3 → 2 → 1
```

---

## Key Integration Points

```
═══════════════════════════════════════════════════════════════════════════════
                    CROSS-SUBSYSTEM API SURFACE
═══════════════════════════════════════════════════════════════════════════════

JOB SYSTEM provides to all subsystems:
├── job_dispatch()           Parallelize any work
├── job_dispatch_to()        Worker affinity for cache locality
├── job_wait_counter()       Dependency barriers
├── job_yield()              Cooperative multitasking
└── job_system_wait_idle()   Frame synchronization

MEMORY provides to all subsystems:
├── arena_alloc()            Zero-overhead per-frame scratch
├── pool_alloc()/pool_get()  Validated persistent object access
└── apool_alloc()/apool_get() Thread-safe resource allocation

NETWORKING provides to simulation:
├── fr_topic_channel_pop()   Consume decoded inbound messages
├── fr_topic_channel_push()  Enqueue outbound messages
├── fr_state_update_queue_pop() Consume decoded commands
└── Quantization:
    ├── net_quantize_vec3_mm()      Position encoding
    ├── net_quantize_quat_snorm16() Rotation encoding
    └── net_dequantize_*()          Decoding

PHYSICS provides to simulation:
├── phys_world_tick()        Run physics pipeline
├── phys_body_pool access    Read/write entity transforms
└── Impact events            Damage, sound triggers

ECS provides to all subsystems:
├── ecs_world_create_entity()      Entity lifecycle
├── ecs_sparse_set_*_insert/get()  Component access
└── Dense array iteration          Cache-coherent batch processing
```
