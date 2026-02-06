# Job System & Memory Management Call Graph

This document provides a complete staged ASCII diagram of the call graph and
execution flow for the Ferrum job system and memory management subsystem. It shows:
- The fiber lifecycle from dispatch through execution to completion
- Work-stealing scheduler mechanics (deterministic + non-deterministic)
- Memory allocator hierarchy (arena, pool, atomic pool)
- Counter-based synchronization and wait semantics
- Full function signatures, data flows, and ownership semantics

---

## Legend

```
┌─────────────────────────────────────────────────────────────────────────────┐
│ LEGEND                                                                      │
├─────────────────────────────────────────────────────────────────────────────┤
│ ═══════  Top-level entry point / system boundary                            │
│ ───────  Synchronous call (caller waits for return)                         │
│ ·······  Fiber context switch (cooperative, resumes later)                  │
│ ┄┄┄┄┄┄┄  Data flow (output becomes input to next stage)                     │
│                                                                             │
│ [SYNC]       Single-threaded synchronous execution                          │
│ [PARALLEL]   Multiple worker threads                                        │
│ [LOCK-FREE]  Atomic CAS-based operation                                     │
│ [LOCKED]     Short mutex-guarded critical section                           │
│ [ASM]        Platform-specific assembly (context swap)                      │
│ [TLS]        Thread-local storage access                                    │
│                                                                             │
│ ──▶       Function call                                                     │
│ ══▶       Data dependency (output flows to next stage input)                │
│ ◆         Sync point (counter reaches zero / wait-idle)                     │
│ ↻         Loop / retry                                                      │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## Data Structures Overview

```
═══════════════════════════════════════════════════════════════════════════════
MEMORY ALLOCATOR HIERARCHY (3-Tier)
═══════════════════════════════════════════════════════════════════════════════

Tier 1: LINEAR ARENA — O(1) alloc, O(1) reset, no individual free
─────────────────────────────────────────────────────────────────

arena_t                              Bump allocator over caller-owned buffer
├── buffer: uint8_t*                 Backing memory (caller-owned)
├── capacity: size_t                 Buffer size in bytes
└── offset: size_t                   Current allocation watermark

Use cases:
├── Physics frame arena (per-tick transient data, ~76 MB)
├── Collision pair scratch buffers
├── Constraint/manifold working sets
└── Any short-lived batch allocation pattern

Lifetime pattern:
    arena_init() → [arena_alloc() × N] → arena_reset()   (per frame)
    arena_mark() / arena_pop_to_mark()                    (nested scopes)


Tier 2: GENERATION POOL — O(1) alloc/free, single-threaded
───────────────────────────────────────────────────────────

pool_t                               Fixed-size element pool
├── storage: uint8_t*                Contiguous element array (heap-allocated)
├── free_list: uint32_t*             Implicit singly-linked list (index chain)
├── generations: uint16_t*           Per-slot generation counter
├── capacity: uint32_t               Maximum elements
├── stride: uint32_t                 Element size in bytes
└── free_head: uint32_t              Head of free list (POOL_INDEX_INVALID when empty)

pool_handle_t                        Validated reference to a pool slot
├── index: uint32_t                  Slot index
├── generation: uint16_t             Must match pool.generations[index]
└── flags: uint16_t                  Reserved

pool_status_t                        Status enum
├── POOL_OK (0)
├── POOL_ERR_OOM (1)
└── POOL_ERR_INVALID (2)

Use cases:
├── Physics body pool (bodies_curr, bodies_next)
├── Collider pool (sphere_pool, box_pool, capsule_pool)
├── ECS entity pool
└── Any fixed-lifetime game object storage

Safety: generation mismatch → pool_get() returns NULL (use-after-free detection)


Tier 3: CONCURRENT ATOMIC POOL — O(1) alloc/free, multi-threaded
─────────────────────────────────────────────────────────────────

apool_t                              Lock-free fixed-size element pool
├── storage: uint8_t*                Contiguous element array (heap-allocated)
├── next: uint32_t*                  Singly-linked stack (index chain)
├── generations: _Atomic uint16_t*   Per-slot generation counter (atomic)
├── free_head: _Atomic uint64_t      Tagged head: { index:32, tag:32 } (ABA prevention)
├── capacity: uint32_t               Maximum elements
└── stride: uint32_t                 Element size in bytes

apool_handle_t                       Validated reference to an apool slot
├── index: uint32_t                  Slot index
├── generation: uint16_t             Must match apool.generations[index]
└── flags: uint16_t                  Reserved

Use cases:
├── Fiber stack pool (job system backbone)
│   └── stride = sizeof(job_fiber_t) + fiber_stack_size
│       Each slot: [job_fiber_t header | stack bytes]
└── Any multi-threaded resource allocation

ABA prevention: 64-bit tagged pointer { index:32, tag:32 }
    CAS on free_head atomically swaps both index and monotonic tag

═══════════════════════════════════════════════════════════════════════════════
JOB SYSTEM STRUCTURES
═══════════════════════════════════════════════════════════════════════════════

job_system_t                         Central scheduler
├── worker_count: uint32_t           Number of OS worker threads
├── queue_capacity: uint32_t         Max runnable jobs in flight
├── fiber_stack_size: size_t         Per-fiber stack size (bytes)
├── fiber_stack_pool: apool_t        Concurrent pool for fiber+stack allocation
├── deterministic: int               1 = single-thread, 0 = multi-worker
├── running: atomic_bool             Lifecycle flag
├── shutting_down: atomic_bool       Graceful shutdown flag
│
│   DETERMINISTIC SCHEDULER (single-threaded):
├── queue: struct job_entry*         Fixed slot array
├── queue_slot_state: atomic_int*    Per-slot: 0=empty, 1=ready, 2=busy
├── queue_insert_cursor: atomic_uint Round-robin insert position
├── queue_pop_cursor: atomic_uint    Round-robin pop position
│
│   NON-DETERMINISTIC SCHEDULER (work-stealing):
├── ws_deques: fr_ws_deque_t*        Per-worker Chase–Lev deque array
├── queued_count: atomic_uint        Total jobs awaiting execution
├── inject_ring: struct job_fiber**  MPSC ring for non-owner enqueues
├── inject_head: uint32_t            Ring head (writers)
├── inject_tail: uint32_t            Ring tail (owner drain)
├── inject_count: uint32_t           Ring occupancy
│
│   SYNCHRONIZATION:
├── queue_lock: mtx_t                Guards inject_ring + cond_wait
├── queue_cond: cnd_t                Wakes idle workers
│
│   DIAGNOSTICS (compile-time FR_JOB_QUEUE_DIAGNOSTICS):
├── qdiag_enqueue_*: atomic_uint64   Enqueue statistics
├── qdiag_pop_*: atomic_uint64       Pop/steal statistics
├── qdiag_cond_waits: atomic_uint64  Condition variable wait count
│
│   WORKER STATE:
├── workers: thrd_t*                 OS thread handles
├── next_job_id: atomic_uint64       Monotonic job ID counter
├── jobs_started: atomic_uint64      Total dispatched
├── jobs_completed: atomic_uint64    Total finished
├── affinity_enabled: atomic_bool    CPU pinning flag
├── numa_enabled: int                NUMA-aware sharding flag
└── numa_node_count: uint32_t        Number of NUMA nodes (≥1)

fr_ws_deque_t                        Chase–Lev work-stealing deque
├── capacity: size_t                 Power-of-2 slot count
├── mask: size_t                     capacity - 1 (fast modulo)
├── top: _Atomic size_t              Steal pointer (thieves access)
├── bottom: _Atomic size_t           Push/pop pointer (owner access)
└── buffer: void**                   Circular pointer array

    Access patterns:
    ├── Owner push/pop: sequential (bottom pointer), no contention
    └── Thief steal: atomic CAS on top pointer, lock-free

job_counter_t                        Waitable synchronization counter
├── value: atomic_uint               Current count
├── lock: mtx_t                      Guards waiter list
└── waiters: void*                   Opaque linked list of parked fibers

job_fiber_t (internal)               Lightweight cooperative coroutine
├── id: uint64_t                     Unique job ID (from next_job_id++)
├── fn: void (*)(void*)              User function pointer
├── user: void*                      User context pointer
├── counter: job_counter_t*          Optional completion counter
├── ctx: job_context_t               Platform-specific register/stack state
├── stack: void*                     Stack memory (within apool slot)
├── handle: apool_handle_t           Pool handle (for deallocation)
├── finished: uint8_t                Completion flag
├── waiting: uint8_t                 Parked flag
├── debug_name: char[]               Tracy zone name (if TRACY_ENABLE)
└── tracy_name_storage[64]: char     Name storage for profiling

job_context_t (internal, x86-64)     Fiber register state
├── rbx, rbp, r12..r15: uint64_t     Callee-saved GPRs
├── sp: uint64_t                     Stack pointer
├── rip: uint64_t                    Resume instruction pointer
├── entry: void*                     Bootstrap entry point (new contexts)
├── arg0: void*                      Bootstrap argument
└── fxsave_area[512]: uint8_t        FPU/SSE state (FXSAVE64)

    Platform support: x86-64 (primary), extensible to ARM64

═══════════════════════════════════════════════════════════════════════════════
THREAD-LOCAL STATE (per worker)
═══════════════════════════════════════════════════════════════════════════════

_Thread_local:
├── g_current_fiber: job_fiber_t*        Active fiber on this worker
├── g_current_system: job_system_t*      System reference
├── g_scheduler_context: job_context_t   Scheduler's saved context
├── g_worker_id: uint32_t               Worker index (UINT32_MAX if not in job)
└── g_worker_node: uint32_t             NUMA node index (0 if disabled)
```

---

## Complete Call Graph

```
═══════════════════════════════════════════════════════════════════════════════
                         MEMORY ALLOCATOR CALL GRAPHS
═══════════════════════════════════════════════════════════════════════════════

┌──────────────────────────────────────────────────────────────────────────────
│ ARENA ALLOCATOR [SYNC, single-threaded]
├──────────────────────────────────────────────────────────────────────────────
│
├──▶ arena_init(arena, buffer, capacity)
│    │
│    │   void arena_init(
│    │       arena_t *arena,    [OUT]  arena to initialize
│    │       void *buffer,      [IN]   caller-owned backing memory
│    │       size_t capacity    [IN]   buffer size in bytes
│    │   )
│    │
│    │   EFFECT: arena->offset = 0, arena->buffer = buffer, arena->capacity = capacity
│    │
│    └── OUTPUT: Arena ready for allocation
│
├──▶ arena_alloc(arena, alignment, size)
│    │
│    │   void *arena_alloc(
│    │       arena_t *arena,    [IN/OUT]  arena state
│    │       size_t alignment,  [IN]      power-of-2 alignment
│    │       size_t size        [IN]      allocation size in bytes
│    │   )
│    │
│    │   ALGORITHM:
│    │   ├── aligned_offset = (arena->offset + alignment - 1) & ~(alignment - 1)
│    │   ├── IF aligned_offset + size > arena->capacity: return NULL
│    │   ├── arena->offset = aligned_offset + size
│    │   └── return arena->buffer + aligned_offset
│    │
│    │   COST: O(1), no syscall, no lock
│    │
│    └── RETURNS: aligned pointer or NULL on exhaustion
│
├──▶ arena_reset(arena)
│    │
│    │   void arena_reset(arena_t *arena)
│    │
│    │   EFFECT: arena->offset = 0
│    │           All prior allocations invalidated (no destructors called)
│    │
│    │   COST: O(1)
│    │
│    └── OUTPUT: Arena empty, ready for next batch
│
├──▶ arena_mark(arena) → size_t
│    │
│    │   size_t arena_mark(const arena_t *arena)
│    │
│    │   RETURNS: current offset (snapshot for nested lifetime)
│    │
│    └── Use: save mark → allocate temporary data → pop_to_mark
│
├──▶ arena_pop_to_mark(arena, mark) → int
│    │
│    │   int arena_pop_to_mark(arena_t *arena, size_t mark)
│    │
│    │   VALIDATES: mark <= arena->offset (else returns -1)
│    │   EFFECT: arena->offset = mark
│    │
│    └── RETURNS: 0 on success, -1 on invalid mark
│
├──────────────────────────────────────────────────────────────────────────────
│ POOL ALLOCATOR [SYNC, single-threaded]
├──────────────────────────────────────────────────────────────────────────────
│
├──▶ pool_init(pool, capacity, stride)
│    │
│    │   pool_status_t pool_init(
│    │       pool_t *pool,       [OUT]  pool to initialize
│    │       uint32_t capacity,  [IN]   number of elements
│    │       uint32_t stride     [IN]   element size in bytes
│    │   )
│    │
│    │   ALLOCATES:
│    │   ├── storage = calloc(capacity, stride)
│    │   ├── free_list = calloc(capacity, sizeof(uint32_t))
│    │   └── generations = calloc(capacity, sizeof(uint16_t))
│    │
│    │   INITIALIZES:
│    │   ├── free_list: [1, 2, 3, ..., POOL_INDEX_INVALID]  (linked chain)
│    │   ├── generations: all 0
│    │   └── free_head = 0
│    │
│    └── RETURNS: POOL_OK or POOL_ERR_OOM
│
├──▶ pool_alloc(pool) → pool_handle_t
│    │
│    │   pool_handle_t pool_alloc(pool_t *pool)
│    │
│    │   ALGORITHM:
│    │   ├── IF free_head == POOL_INDEX_INVALID: return invalid handle
│    │   ├── index = free_head
│    │   ├── free_head = free_list[index]   (advance free list)
│    │   ├── generations[index]++           (increment generation)
│    │   └── return { index, generations[index], 0 }
│    │
│    │   COST: O(1)
│    │
│    └── RETURNS: valid handle or { POOL_INDEX_INVALID, 0, 0 }
│
├──▶ pool_free(pool, handle) → pool_status_t
│    │
│    │   pool_status_t pool_free(pool_t *pool, pool_handle_t handle)
│    │
│    │   ALGORITHM:
│    │   ├── VALIDATE: handle.index < capacity
│    │   ├── VALIDATE: handle.generation == generations[handle.index]
│    │   ├── free_list[handle.index] = free_head   (push onto free list)
│    │   ├── free_head = handle.index
│    │   └── generations[handle.index]++           (invalidate stale handles)
│    │
│    │   COST: O(1)
│    │
│    └── RETURNS: POOL_OK or POOL_ERR_INVALID
│
├──▶ pool_get(pool, handle) → void*
│    │
│    │   void *pool_get(const pool_t *pool, pool_handle_t handle)
│    │
│    │   ALGORITHM:
│    │   ├── IF handle.index >= capacity: return NULL
│    │   ├── IF handle.generation != generations[handle.index]: return NULL
│    │   └── return storage + handle.index * stride
│    │
│    │   COST: O(1)
│    │
│    └── RETURNS: element pointer or NULL (stale handle → use-after-free detected)
│
├──▶ pool_destroy(pool)
│    │   free(storage), free(free_list), free(generations)
│    └── EFFECT: All pool memory released
│
├──────────────────────────────────────────────────────────────────────────────
│ ATOMIC POOL ALLOCATOR [LOCK-FREE, multi-threaded]
├──────────────────────────────────────────────────────────────────────────────
│
├──▶ apool_init(pool, capacity, stride)
│    │
│    │   apool_status_t apool_init(
│    │       apool_t *pool,      [OUT]  pool to initialize
│    │       uint32_t capacity,  [IN]   number of elements
│    │       uint32_t stride     [IN]   element size in bytes
│    │   )
│    │
│    │   ALLOCATES:
│    │   ├── storage = calloc(capacity, stride)
│    │   ├── next = calloc(capacity, sizeof(uint32_t))
│    │   └── generations = calloc(capacity, sizeof(_Atomic uint16_t))
│    │
│    │   INITIALIZES:
│    │   ├── next: [1, 2, 3, ..., APOOL_INDEX_INVALID]  (singly-linked stack)
│    │   ├── generations: all 0 (atomic)
│    │   └── free_head = pack64(index=0, tag=0)
│    │
│    └── RETURNS: APOOL_OK or APOOL_ERR_OOM
│
├──▶ apool_alloc(pool) → apool_handle_t                       [LOCK-FREE]
│    │
│    │   apool_handle_t apool_alloc(apool_t *pool)
│    │
│    │   ALGORITHM:
│    │   ↻ CAS retry loop:
│    │   │   ├── old_head = atomic_load(&pool->free_head)
│    │   │   ├── index = unpack_index(old_head)
│    │   │   ├── IF index == APOOL_INDEX_INVALID: return invalid (pool exhausted)
│    │   │   ├── new_index = pool->next[index]
│    │   │   ├── new_tag = unpack_tag(old_head) + 1
│    │   │   ├── new_head = pack64(new_index, new_tag)
│    │   │   └── IF atomic_compare_exchange_weak(&free_head, &old_head, new_head):
│    │   │       ├── gen = atomic_fetch_add(&generations[index], 1) + 1
│    │   │       └── return { index, gen, 0 }
│    │   └── ELSE: ↻ retry (another thread won the CAS race)
│    │
│    │   ABA PREVENTION: tag monotonically increments → stale CAS fails
│    │   COST: O(1) amortized, CAS retries bounded by thread count
│    │
│    └── RETURNS: valid handle or { APOOL_INDEX_INVALID, 0, 0 }
│
├──▶ apool_free(pool, handle) → apool_status_t                [LOCK-FREE]
│    │
│    │   apool_status_t apool_free(apool_t *pool, apool_handle_t handle)
│    │
│    │   ALGORITHM:
│    │   ├── VALIDATE: handle.index < capacity
│    │   ├── VALIDATE: handle.generation == atomic_load(&generations[handle.index])
│    │   ↻ CAS retry loop:
│    │   │   ├── old_head = atomic_load(&pool->free_head)
│    │   │   ├── pool->next[handle.index] = unpack_index(old_head)
│    │   │   ├── new_head = pack64(handle.index, unpack_tag(old_head) + 1)
│    │   │   └── IF atomic_compare_exchange_weak(&free_head, &old_head, new_head):
│    │   │       └── return APOOL_OK
│    │   └── ELSE: ↻ retry
│    │
│    │   EFFECT: Slot returned to free stack, generation already bumped on alloc
│    │
│    └── RETURNS: APOOL_OK or APOOL_ERR_INVALID
│
├──▶ apool_get(pool, handle) → void*                          [LOCK-FREE]
│    │
│    │   void *apool_get(const apool_t *pool, apool_handle_t handle)
│    │
│    │   ALGORITHM:
│    │   ├── IF handle.index >= capacity: return NULL
│    │   ├── IF handle.generation != atomic_load(&generations[handle.index]): return NULL
│    │   └── return storage + handle.index * stride
│    │
│    └── RETURNS: element pointer or NULL (stale handle detected)

═══════════════════════════════════════════════════════════════════════════════
                         JOB SYSTEM CALL GRAPH
═══════════════════════════════════════════════════════════════════════════════

┌──────────────────────────────────────────────────────────────────────────────
│ STAGE J0: SYSTEM CREATION [SYNC] — one-time initialization
├──────────────────────────────────────────────────────────────────────────────
│
├──▶ job_system_create(sys, worker_count, queue_capacity,
│    │                  fiber_stack_size, fiber_count_max, deterministic_mode)
│    │
│    │   job_system_create_status_t job_system_create(
│    │       job_system_t *sys,           [OUT]  system to initialize
│    │       uint32_t worker_count,       [IN]   OS worker thread count (≥1)
│    │       uint32_t queue_capacity,     [IN]   max runnable jobs
│    │       size_t fiber_stack_size,     [IN]   per-fiber stack (≥16384)
│    │       size_t fiber_count_max,      [IN]   max concurrent fibers
│    │       int deterministic_mode       [IN]   1=single-thread, 0=multi-worker
│    │   )
│    │
│    │   ALLOCATES:
│    │   ├──▶ apool_init(&sys->fiber_stack_pool, fiber_count_max,
│    │   │               sizeof(job_fiber_t) + fiber_stack_size)
│    │   │    │
│    │   │    │   Each apool slot layout:
│    │   │    │   ┌────────────────────┬─────────────────────────┐
│    │   │    │   │ job_fiber_t header │ stack_bytes              │
│    │   │    │   │ (~256 bytes)       │ (fiber_stack_size bytes) │
│    │   │    │   └────────────────────┴─────────────────────────┘
│    │   │    │
│    │   │    └── EFFECT: Lock-free fiber allocation pool ready
│    │   │
│    │   ├── mtx_init(&sys->queue_lock)
│    │   ├── cnd_init(&sys->queue_cond)
│    │   │
│    │   ├── IF deterministic:
│    │   │   ├── calloc(queue_capacity, sizeof(job_entry))
│    │   │   └── calloc(queue_capacity, sizeof(atomic_int))   [slot states]
│    │   │
│    │   └── IF non-deterministic:
│    │       ├── calloc(worker_count, sizeof(fr_ws_deque_t))
│    │       │   └── For each worker:
│    │       │       └──▶ fr_ws_deque_init(&ws_deques[i], queue_capacity / worker_count)
│    │       │            │   Rounds capacity up to next power-of-2
│    │       │            └── EFFECT: Per-worker Chase–Lev deque ready
│    │       │
│    │       └── calloc(queue_capacity, sizeof(job_fiber_t*))  [inject ring]
│    │
│    └── RETURNS: JOB_CREATE_OK or error status
│
├──────────────────────────────────────────────────────────────────────────────
│ STAGE J1: SYSTEM START [SYNC → PARALLEL] — spawns OS threads
├──────────────────────────────────────────────────────────────────────────────
│
├──▶ job_system_start(sys)
│    │
│    │   int job_system_start(job_system_t *sys)
│    │
│    │   ALGORITHM:
│    │   ├── atomic_store(&sys->running, true)
│    │   ├── sys->workers = calloc(worker_count, sizeof(thrd_t))
│    │   ├── For each worker i in [0..worker_count):
│    │   │   └──▶ thrd_create(&workers[i], worker_main, worker_args[i])
│    │   │        │
│    │   │        │   SPAWNED THREAD:
│    │   │        │   └── worker_main(sys, worker_id)  [see STAGE J3]
│    │   │        │
│    │   │        └── EFFECT: OS thread running, enters work-steal loop
│    │   │
│    │   ├── IF affinity_enabled:
│    │   │   └── For each worker: sched_setaffinity(worker_id → cpu_id)
│    │   │
│    │   └── IF numa_enabled:
│    │       └── Set g_worker_node = worker_id % numa_node_count per thread
│    │
│    └── RETURNS: 0 on success, -1 on thread creation failure
│
├──────────────────────────────────────────────────────────────────────────────
│ STAGE J2: JOB DISPATCH [SYNC/LOCK-FREE] — ~0.5-2 µs
├──────────────────────────────────────────────────────────────────────────────
│
├──▶ job_dispatch(sys, fn, user_data, priority, counter) → job_id_t
│    │
│    │   job_id_t job_dispatch(
│    │       job_system_t *sys,              [IN/OUT]
│    │       void (*fn)(void *),             [IN]  job function
│    │       void *user_data,                [IN]  user context
│    │       int priority,                   [IN]  priority hint
│    │       struct job_counter *counter      [IN]  optional sync counter
│    │   )
│    │
│    │   CALLS:
│    │   │
│    │   ├──▶ job_fiber_create_named(sys, fn, user_data, counter, priority, id, NULL)
│    │   │    │
│    │   │    │   INTERNALLY:
│    │   │    │   ├──▶ apool_alloc(&sys->fiber_stack_pool)       [LOCK-FREE]
│    │   │    │   │    └── OUTPUT: apool_handle_t (fiber+stack slot)
│    │   │    │   │
│    │   │    │   ├──▶ apool_get(&sys->fiber_stack_pool, handle)
│    │   │    │   │    └── OUTPUT: job_fiber_t* (pointer to slot)
│    │   │    │   │
│    │   │    │   ├── fiber->fn = fn
│    │   │    │   ├── fiber->user = user_data
│    │   │    │   ├── fiber->counter = counter
│    │   │    │   ├── fiber->id = id
│    │   │    │   ├── fiber->finished = 0
│    │   │    │   ├── fiber->waiting = 0
│    │   │    │   ├── fiber->stack = (uint8_t*)fiber + sizeof(job_fiber_t)
│    │   │    │   │
│    │   │    │   ├──▶ job_context_init(&fiber->ctx, fiber_trampoline, fiber->id,
│    │   │    │   │                     fiber->stack, sys->fiber_stack_size)   [ASM]
│    │   │    │   │    │
│    │   │    │   │    │   SETS UP:
│    │   │    │   │    │   ├── ctx->sp = stack_top - red_zone
│    │   │    │   │    │   ├── ctx->rip = fiber_trampoline
│    │   │    │   │    │   ├── ctx->entry = fiber_trampoline
│    │   │    │   │    │   └── ctx->arg0 = fiber_id
│    │   │    │   │    │
│    │   │    │   │    └── EFFECT: Context ready for first swap-in
│    │   │    │   │
│    │   │    │   └── OUTPUT: initialized job_fiber_t*
│    │   │    │
│    │   │    └── OUTPUT ══════════════════════════════════════════════════▶
│    │   │                job_fiber_t* (ready to enqueue)
│    │   │
│    │   ├── IF counter != NULL:
│    │   │   └──▶ job_counter_add(counter, 1)
│    │   │        └── EFFECT: atomic_fetch_add(&counter->value, 1)
│    │   │
│    │   ├──▶ job_system_enqueue_preferred(sys, fiber, priority, id, worker_id)
│    │   │    │
│    │   │    │   IF deterministic:
│    │   │    │   └──▶ enqueue_deterministic(sys, fiber)
│    │   │    │        │
│    │   │    │        │   ALGORITHM:
│    │   │    │        │   ↻ Scan from queue_insert_cursor:
│    │   │    │        │   │   ├── IF queue_slot_state[i] == 0 (empty):
│    │   │    │        │   │   │   ├── CAS slot_state 0 → 1 (ready)
│    │   │    │        │   │   │   ├── queue[i].fiber = fiber
│    │   │    │        │   │   │   └── BREAK
│    │   │    │        │   │   └── i = (i + 1) % queue_capacity
│    │   │    │        │   └── IF full scan with no empty slot: return error
│    │   │    │        │
│    │   │    │        └── EFFECT: Fiber placed in slot, state = ready
│    │   │    │
│    │   │    │   IF non-deterministic:
│    │   │    │   └──▶ enqueue_ws(sys, fiber, preferred_worker)
│    │   │    │        │
│    │   │    │        │   ALGORITHM:
│    │   │    │        │   ├── target = preferred_worker (or current worker if UINT32_MAX)
│    │   │    │        │   ├── IF caller IS owner of ws_deques[target]:
│    │   │    │        │   │   └──▶ fr_ws_deque_push(&ws_deques[target], fiber) [LOCK-FREE]
│    │   │    │        │   │        │
│    │   │    │        │   │        │   ALGORITHM:
│    │   │    │        │   │        │   ├── bottom = atomic_load(&dq->bottom)
│    │   │    │        │   │        │   ├── buffer[bottom & mask] = fiber
│    │   │    │        │   │        │   ├── atomic_store(&dq->bottom, bottom + 1)
│    │   │    │        │   │        │   └── IF bottom - top >= capacity: return -1 (full)
│    │   │    │        │   │        │
│    │   │    │        │   │        └── EFFECT: Fiber at bottom of owner's deque
│    │   │    │        │   │
│    │   │    │        │   └── ELSE (non-owner enqueue):
│    │   │    │        │       └── mtx_lock(&queue_lock)                    [LOCKED]
│    │   │    │        │           ├── inject_ring[inject_head] = fiber
│    │   │    │        │           ├── inject_head = (inject_head + 1) % queue_capacity
│    │   │    │        │           ├── inject_count++
│    │   │    │        │           └── mtx_unlock(&queue_lock)
│    │   │    │        │
│    │   │    │        └── EFFECT: Fiber enqueued for execution
│    │   │    │
│    │   │    ├── atomic_fetch_add(&sys->queued_count, 1)
│    │   │    └──▶ cnd_broadcast(&sys->queue_cond)
│    │   │         └── EFFECT: Wake idle workers
│    │   │
│    │   └── atomic_fetch_add(&sys->jobs_started, 1)
│    │
│    └── RETURNS: job_id (unique, monotonic) or JOB_ID_INVALID
│
│   VARIANTS:
│
├──▶ job_dispatch_named(sys, fn, user_data, priority, counter, debug_name)
│    │   Same as job_dispatch + copies debug_name into fiber->tracy_name_storage
│    └── RETURNS: job_id_t
│
├──▶ job_dispatch_to(sys, fn, user_data, priority, counter, preferred_worker)
│    │   Same as job_dispatch + routes to specific worker's deque
│    └── RETURNS: job_id_t
│
├──────────────────────────────────────────────────────────────────────────────
│ STAGE J3: WORKER EXECUTION LOOP [PARALLEL, per OS thread] — continuous
├──────────────────────────────────────────────────────────────────────────────
│
├──▶ worker_main(sys, worker_id)
│    │
│    │   THREAD-LOCAL SETUP:                                       [TLS]
│    │   ├── g_current_system = sys
│    │   ├── g_worker_id = worker_id
│    │   ├── g_worker_node = worker_id % numa_node_count
│    │   └── g_scheduler_context = {} (initialized on first swap)
│    │
│    │   MAIN LOOP (while atomic_load(&sys->running)):
│    │   │
│    │   ├──▶ job_system_pop_next(sys, &entry)
│    │   │    │
│    │   │    │   IF deterministic:
│    │   │    │   └──▶ pop_deterministic(sys, &entry)
│    │   │    │        │
│    │   │    │        │   ALGORITHM:
│    │   │    │        │   ↻ Scan from queue_pop_cursor:
│    │   │    │        │   │   ├── IF queue_slot_state[i] == 1 (ready):
│    │   │    │        │   │   │   ├── CAS slot_state 1 → 2 (busy)
│    │   │    │        │   │   │   ├── entry->fiber = queue[i].fiber
│    │   │    │        │   │   │   └── BREAK
│    │   │    │        │   │   └── i = (i + 1) % queue_capacity
│    │   │    │        │   └── IF no ready slot: wait on queue_cond
│    │   │    │        │
│    │   │    │        └── OUTPUT: job_entry with fiber pointer
│    │   │    │
│    │   │    │   IF non-deterministic:
│    │   │    │   └──▶ pop_ws(sys, worker_id, &entry)
│    │   │    │        │
│    │   │    │        │   ALGORITHM (priority order):
│    │   │    │        │   │
│    │   │    │        │   ├── 1. TRY POP INJECTED:
│    │   │    │        │   │   └── mtx_lock(&queue_lock)             [LOCKED]
│    │   │    │        │   │       ├── IF inject_count > 0:
│    │   │    │        │   │       │   ├── fiber = inject_ring[inject_tail]
│    │   │    │        │   │       │   ├── inject_tail = (inject_tail + 1) % cap
│    │   │    │        │   │       │   ├── inject_count--
│    │   │    │        │   │       │   └── RETURN fiber
│    │   │    │        │   │       └── mtx_unlock(&queue_lock)
│    │   │    │        │   │
│    │   │    │        │   ├── 2. TRY OWN DEQUE (owner pop):
│    │   │    │        │   │   └──▶ fr_ws_deque_pop(&ws_deques[worker_id]) [LOCK-FREE]
│    │   │    │        │   │        │
│    │   │    │        │   │        │   ALGORITHM (Chase–Lev):
│    │   │    │        │   │        │   ├── bottom = atomic_load(&dq->bottom) - 1
│    │   │    │        │   │        │   ├── atomic_store(&dq->bottom, bottom)
│    │   │    │        │   │        │   ├── top = atomic_load(&dq->top)
│    │   │    │        │   │        │   ├── IF top <= bottom: return buffer[bottom & mask]
│    │   │    │        │   │        │   ├── IF top == bottom:
│    │   │    │        │   │        │   │   ├── CAS top → top+1 (race with thieves)
│    │   │    │        │   │        │   │   ├── IF CAS succeeds: return last item
│    │   │    │        │   │        │   │   └── ELSE: empty, restore bottom = top+1
│    │   │    │        │   │        │   └── IF top > bottom: empty, restore bottom = top
│    │   │    │        │   │        │
│    │   │    │        │   │        └── RETURNS: fiber pointer or NULL
│    │   │    │        │   │
│    │   │    │        │   ├── 3. TRY STEAL (from random other workers):
│    │   │    │        │   │   └── For j in random_permutation(0..worker_count):
│    │   │    │        │   │       └──▶ fr_ws_deque_steal(&ws_deques[j])   [LOCK-FREE]
│    │   │    │        │   │            │
│    │   │    │        │   │            │   ALGORITHM (Chase–Lev):
│    │   │    │        │   │            │   ├── top = atomic_load(&dq->top)
│    │   │    │        │   │            │   ├── bottom = atomic_load(&dq->bottom)
│    │   │    │        │   │            │   ├── IF top >= bottom: return NULL (empty)
│    │   │    │        │   │            │   ├── item = buffer[top & mask]
│    │   │    │        │   │            │   ├── CAS top → top+1
│    │   │    │        │   │            │   └── IF CAS succeeds: return item
│    │   │    │        │   │            │       ELSE: return NULL (lost race)
│    │   │    │        │   │            │
│    │   │    │        │   │            └── RETURNS: stolen fiber or NULL
│    │   │    │        │   │
│    │   │    │        │   └── 4. SLEEP:
│    │   │    │        │       └── mtx_lock → cnd_wait(&queue_cond) → mtx_unlock
│    │   │    │        │           └── EFFECT: Thread parked until woken by dispatch
│    │   │    │        │
│    │   │    │        └── OUTPUT: fiber pointer
│    │   │    │
│    │   │    └── OUTPUT ══════════════════════════════════════════════════▶
│    │   │                job_fiber_t* (ready to execute)
│    │   │
│    │   ├──▶ run_entry(sys, entry, &g_scheduler_context)
│    │   │    │
│    │   │    │   SETUP:
│    │   │    │   ├── g_current_fiber = entry->fiber                  [TLS]
│    │   │    │   │
│    │   │    │   ├──▶ job_context_swap(&g_scheduler_context, &fiber->ctx)   [ASM]
│    │   │    │   │    │
│    │   │    │   │    │   SAVES: all callee-saved regs + sp + rip + FPU state
│    │   │    │   │    │   LOADS: fiber's saved context
│    │   │    │   │    │
│    │   │    │   │    │   IF first swap (new fiber):
│    │   │    │   │    │   └── Jumps to fiber_trampoline → fiber_trampoline_body()
│    │   │    │   │    │
│    │   │    │   │    │   IF resuming (after yield/wait):
│    │   │    │   │    │   └── Returns to saved rip in fiber's execution
│    │   │    │   │    │
│    │   │    │   │    └── EFFECT: Execution transfers to fiber
│    │   │    │   │
│    │   │    │   │   ┄┄┄ FIBER EXECUTES ┄┄┄
│    │   │    │   │
│    │   │    │   │   fiber_trampoline_body(fiber):
│    │   │    │   │   ├── TracyCFiberEnter(fiber->debug_name)     [if TRACY_ENABLE]
│    │   │    │   │   │
│    │   │    │   │   ├──▶ fiber->fn(fiber->user)                ← USER CODE RUNS
│    │   │    │   │   │    │
│    │   │    │   │   │    │   User code may:
│    │   │    │   │   │    │   ├── Call job_dispatch() to spawn child jobs
│    │   │    │   │   │    │   ├── Call job_wait_counter() to park [see STAGE J4]
│    │   │    │   │   │    │   ├── Call job_yield() to cooperatively yield
│    │   │    │   │   │    │   └── Return normally when done
│    │   │    │   │   │    │
│    │   │    │   │   │    └── RETURNS: void (fiber function completed)
│    │   │    │   │   │
│    │   │    │   │   ├── fiber->finished = 1
│    │   │    │   │   │
│    │   │    │   │   ├── IF fiber->counter != NULL:
│    │   │    │   │   │   └──▶ job_counter_dec(fiber->counter)
│    │   │    │   │   │        │
│    │   │    │   │   │        │   ALGORITHM:
│    │   │    │   │   │        │   ├── old = atomic_fetch_sub(&counter->value, 1)
│    │   │    │   │   │        │   ├── IF old == 1 (counter reached zero):
│    │   │    │   │   │        │   │   └──▶ job_system_wake_waiters(counter)
│    │   │    │   │   │        │   │        │   EFFECT: All fibers parked on this
│    │   │    │   │   │        │   │        │   counter are re-enqueued for execution
│    │   │    │   │   │        │   │        └── ◆ Waiters unparked
│    │   │    │   │   │        │   │
│    │   │    │   │   │        │   └── RETURNS: 0 on success
│    │   │    │   │   │        │
│    │   │    │   │   │        └── EFFECT: Counter decremented; may wake dependents
│    │   │    │   │   │
│    │   │    │   │   ├── atomic_fetch_add(&sys->jobs_completed, 1)
│    │   │    │   │   │
│    │   │    │   │   ├──▶ apool_free(&sys->fiber_stack_pool, fiber->handle) [LOCK-FREE]
│    │   │    │   │   │    └── EFFECT: Fiber+stack slot returned to pool
│    │   │    │   │   │
│    │   │    │   │   └──▶ job_context_swap(&fiber->ctx, &g_scheduler_context)  [ASM]
│    │   │    │   │        └── EFFECT: Return to worker loop
│    │   │    │   │
│    │   │    │   └── g_current_fiber = NULL                          [TLS]
│    │   │    │
│    │   │    └── atomic_fetch_sub(&sys->queued_count, 1)
│    │   │
│    │   └── Continue loop → pop next job
│    │
│    └── EXITS: when atomic_load(&sys->running) == false
│
├──────────────────────────────────────────────────────────────────────────────
│ STAGE J4: SYNCHRONIZATION (wait / yield / wake) [SYNC/FIBER]
├──────────────────────────────────────────────────────────────────────────────
│
├──▶ job_wait_counter(counter, spin_count) → job_wait_status_t
│    │
│    │   job_wait_status_t job_wait_counter(
│    │       job_counter_t *counter,    [IN/OUT]
│    │       uint32_t spin_count        [IN]  spin iterations before parking
│    │   )
│    │
│    │   ALGORITHM:
│    │   ├── IF atomic_load(&counter->value) == 0: return JOB_WAIT_OK (fast path)
│    │   │
│    │   ├── Spin loop (spin_count iterations):
│    │   │   ├── IF counter->value == 0: return JOB_WAIT_OK
│    │   │   └── pause / yield hint
│    │   │
│    │   ├── PARK FIBER:                                           [LOCKED]
│    │   │   ├── mtx_lock(&counter->lock)
│    │   │   ├── IF counter->value == 0: unlock, return OK (race resolved)
│    │   │   ├── Add current fiber to counter->waiters list
│    │   │   ├── fiber->waiting = 1
│    │   │   ├── mtx_unlock(&counter->lock)
│    │   │   └──▶ job_context_swap(&fiber->ctx, &g_scheduler_context) [ASM]
│    │   │        │
│    │   │        │   EFFECT: Fiber yields to scheduler.
│    │   │        │   Worker thread continues popping other jobs.
│    │   │        │   Fiber will be re-enqueued when counter reaches 0.
│    │   │        │
│    │   │        └── ◆ (resumes here when woken)
│    │   │
│    │   └── return JOB_WAIT_OK (after waking)
│    │
│    └── RETURNS: JOB_WAIT_OK / JOB_WAIT_TIMEOUT / JOB_WAIT_INVALID
│
├──▶ job_yield()
│    │
│    │   void job_yield(void)
│    │
│    │   MUST be called from within a running job fiber.
│    │
│    │   ALGORITHM:
│    │   ├── fiber = g_current_fiber                                [TLS]
│    │   ├── Re-enqueue fiber for later execution
│    │   └──▶ job_context_swap(&fiber->ctx, &g_scheduler_context)  [ASM]
│    │        │
│    │        │   EFFECT: Fiber parks, worker picks up next job.
│    │        │   Fiber will be popped again later by same or different worker.
│    │        │
│    │        └── (resumes here when re-scheduled)
│    │
│    └── RETURNS: void (after resuming)
│
├──▶ job_counter_init(counter, initial)
│    │   Initialize counter with initial value, init mutex
│    └── EFFECT: counter->value = initial, counter->waiters = NULL
│
├──▶ job_counter_destroy(counter)
│    │   Destroy mutex, assert no waiters
│    └── EFFECT: Internal resources freed
│
├──▶ job_counter_value(counter) → uint32_t
│    │   Snapshot of current counter value
│    └── RETURNS: atomic_load(&counter->value)
│
├──────────────────────────────────────────────────────────────────────────────
│ STAGE J5: SYSTEM SHUTDOWN [SYNC → BARRIER] — graceful teardown
├──────────────────────────────────────────────────────────────────────────────
│
├──▶ job_system_wait_idle(sys)
│    │
│    │   int job_system_wait_idle(job_system_t *sys)
│    │
│    │   ALGORITHM:
│    │   ├── IF deterministic:
│    │   │   └── LOOP: manually pop and execute jobs until
│    │   │       queued_count == 0 AND jobs_started == jobs_completed
│    │   │
│    │   └── IF multi-worker:
│    │       └── SPIN: wait until
│    │           queued_count == 0 AND jobs_started == jobs_completed
│    │           (workers drain remaining jobs)
│    │
│    │   ◆ All dispatched jobs complete
│    │
│    └── RETURNS: 0 on success
│
├──▶ job_system_shutdown(sys)
│    │
│    │   void job_system_shutdown(job_system_t *sys)
│    │
│    │   ALGORITHM:
│    │   ├── atomic_store(&sys->shutting_down, true)
│    │   ├── atomic_store(&sys->running, false)
│    │   ├──▶ cnd_broadcast(&sys->queue_cond)
│    │   │    └── EFFECT: Wake all sleeping workers
│    │   ├── For each worker:
│    │   │   └──▶ thrd_join(&sys->workers[i], &result)
│    │   │        └── ◆ Worker thread exits
│    │   │
│    │   ├── CLEANUP:
│    │   │   ├──▶ apool_destroy(&sys->fiber_stack_pool)
│    │   │   ├── free(ws_deques) / free(queue)
│    │   │   ├── free(inject_ring)
│    │   │   ├── free(workers)
│    │   │   ├── mtx_destroy(&queue_lock)
│    │   │   └── cnd_destroy(&queue_cond)
│    │   │
│    │   └── For each ws_deque:
│    │       └──▶ fr_ws_deque_destroy(&ws_deques[i])
│    │
│    └── EFFECT: System fully torn down, all memory freed
│
├──────────────────────────────────────────────────────────────────────────────
│ STAGE J6: AFFINITY & NUMA [SYNC]
├──────────────────────────────────────────────────────────────────────────────
│
├──▶ job_system_enable_affinity(sys, enable) → int
│    │   Enable/disable CPU pinning for worker threads
│    │   Uses sched_setaffinity() on Linux
│    └── EFFECT: Workers pinned to CPU cores (reduced cache thrash)
│
├──▶ job_system_enable_numa(sys, node_count) → int
│    │   Simulated NUMA topology: worker_id % node_count → node
│    │   Enqueue prefers local node's deque region
│    │   Steal falls back to global (any node)
│    └── EFFECT: NUMA-aware job placement
│
├──▶ job_system_enable_numa_auto(sys) → int
│    │   Auto-detect from /sys/devices/system/node/
│    │   Env override: JOB_SYS_NUMA_SYSFS
│    └── EFFECT: NUMA enabled if ≥2 nodes detected
│
├──────────────────────────────────────────────────────────────────────────────
│ STAGE J7: DIAGNOSTICS & INSTRUMENTATION
├──────────────────────────────────────────────────────────────────────────────
│
├──▶ job_system_queue_diag_snapshot(sys, &out)
│    │   Copy atomic diagnostic counters into snapshot struct
│    │   (enqueue calls, scanned slots, claim failures, pop stats, cond waits)
│    └── RETURNS: job_queue_diag_snapshot_t (all zeros if not compiled in)
│
├──▶ job_system_queue_diag_reset(sys)
│    │   Reset all diagnostic counters to zero
│    └── EFFECT: Clean slate for next measurement window
│
├──▶ job_system_queue_is_lock_free(sys) → int
│    │   1 if enqueue/dequeue use lock-free atomics (non-deterministic mode)
│    └── 0 if mutex-based (deterministic mode)
│
├──▶ job_system_queue_is_sharded(sys) → int
│    │   1 if per-worker Chase–Lev deques active
│    └── 0 if single shared queue
│
│   Tracy integration (compile-time TRACY_ENABLE):
│   ├── TracyCFiberEnter(fiber->debug_name)    on fiber swap-in
│   ├── TracyCZoneBegin(...)                   on fiber entry
│   ├── TracyCZoneEnd(...)                     on fiber exit
│   └── TracyCFiberLeave                       on fiber swap-out
│
│   Instrumentation (compile-time FR_JOB_INSTRUMENTATION):
│   └── job_instrument_event(type, ...) logged for: enqueue, pop, inject, steal, wait
```

---

## Typical Usage Patterns

```
═══════════════════════════════════════════════════════════════════════════════
PATTERN 1: FORK-JOIN (parallel batch processing)
═══════════════════════════════════════════════════════════════════════════════

    job_counter_t done;
    job_counter_init(&done, 0);

    for (int i = 0; i < N; i++) {
        job_dispatch(sys, process_chunk, &args[i], 0, &done);
    }
    // N jobs dispatched, counter = N

    job_wait_counter(&done, 0);   // Parks fiber until all N complete
    // ◆ counter = 0, all chunks processed

    job_counter_destroy(&done);

    Timeline:
    ┌──────────┐ ┌──────────┐ ┌──────────┐
    │ chunk 0  │ │ chunk 1  │ │ chunk 2  │  ... (N parallel)
    └────┬─────┘ └────┬─────┘ └────┬─────┘
         └────────────┴────────────┘
                      │
                 ◆ wait_counter
                      │
              [continuation]

═══════════════════════════════════════════════════════════════════════════════
PATTERN 2: PIPELINE (staged dependencies)
═══════════════════════════════════════════════════════════════════════════════

    job_counter_t stage1_done, stage2_done;
    job_counter_init(&stage1_done, 0);
    job_counter_init(&stage2_done, 0);

    // Stage 1: broadphase
    for (int i = 0; i < M; i++)
        job_dispatch(sys, broadphase_chunk, &args1[i], 0, &stage1_done);

    // Stage 2: narrowphase (depends on stage 1)
    for (int i = 0; i < M; i++) {
        // Each narrowphase job waits on stage1_done internally
        args2[i].wait_counter = &stage1_done;
        job_dispatch(sys, narrowphase_chunk, &args2[i], 0, &stage2_done);
    }

    job_wait_counter(&stage2_done, 0);
    // ◆ Both stages complete

    job_counter_destroy(&stage1_done);
    job_counter_destroy(&stage2_done);

═══════════════════════════════════════════════════════════════════════════════
PATTERN 3: PHYSICS TICK (arena + pool + jobs)
═══════════════════════════════════════════════════════════════════════════════

    // Per-tick arena allocation
    arena_reset(&frame_arena);

    phys_collision_pair_t *pairs = arena_alloc(&frame_arena,
        _Alignof(phys_collision_pair_t),
        max_pairs * sizeof(phys_collision_pair_t));

    // Pool-managed persistent bodies
    pool_handle_t h = pool_alloc(&body_pool);
    phys_body_t *body = pool_get(&body_pool, h);

    // Parallel broadphase via job system
    job_counter_t bp_done;
    job_counter_init(&bp_done, 0);
    for (int i = 0; i < num_chunks; i++)
        job_dispatch(sys, broadphase_job, &chunks[i], 0, &bp_done);
    job_wait_counter(&bp_done, 0);

    // Arena data valid until next arena_reset()
    // Pool data valid until pool_free(h)

═══════════════════════════════════════════════════════════════════════════════
PATTERN 4: FIBER STACK LIFECYCLE
═══════════════════════════════════════════════════════════════════════════════

    apool slot layout:
    ┌─────────────────────────────────────────────────────┐
    │         stride = sizeof(job_fiber_t) + stack_size   │
    ├──────────────────┬──────────────────────────────────┤
    │ job_fiber_t      │ stack_bytes                      │
    │ (header, ctx,    │ (grows downward from top)        │
    │  fn, user,       │                                  │
    │  counter, etc.)  │ fiber->ctx.sp = top - red_zone   │
    │ ~256 bytes       │ fiber_stack_size bytes            │
    └──────────────────┴──────────────────────────────────┘

    Lifecycle:
    1. apool_alloc() → handle          [LOCK-FREE, any thread]
    2. apool_get(handle) → fiber*      [LOCK-FREE]
    3. Initialize fiber fields + context_init()
    4. Enqueue to deque/inject_ring
    5. Worker pops → context_swap into fiber
    6. fiber->fn(fiber->user) executes
    7. On completion: counter_dec + apool_free(handle) [LOCK-FREE]
    8. context_swap back to scheduler
```
