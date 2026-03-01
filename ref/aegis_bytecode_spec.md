# AEGIS Bytecode Scripting Language Specification v5.0
## Event-Driven, Traced, Validated Scripting for Fiber-Based Game Engines

---

## 1. Design Goals

AEGIS is a bytecode scripting language designed for secure, deterministic, traced execution within existing fiber-based game engines. It is:

- **Event-driven**: Scripts respond to events (prefixed with `!`) rather than polling entity state
- **Data-oriented**: Operates on read-only world snapshots via `entity_attrs_t`, returns state updates
- **Tracing-native**: Every execution produces content-addressable trace graphs
- **Sandboxed**: Per-script arena memory, no raw pointers, bounds-checked on every access
- **Yield-mandatory**: Must yield per server tick; fuel-metered with wall-time backstop
- **Fiber-integrated**: Scripts run on engine fibers (`job_dispatch`), yielding the fiber on `wait`

The language integrates with existing engine infrastructure (sparse-set ECS, `entity_attrs_t` dynamic attributes, lock-free command rings, fiber scheduler) without reimplementing them.
This document describes the scripting engine and bytecode interpreter on top of which the language will be implemented.

---

## 2. Execution Model

### 2.1 Script Lifecycle

A script is a **coroutine** that subscribes to an event topic and processes events in a loop. The entry point is `resume` (not `call`). The script runs until it `yield`s back to the scheduler or `exit`s permanently.

```
subscribe(!topic) → Engine routes matching events to script's queue
                    ↓
resume(state)     → Script receives (event, world_view) pair
                    ↓
execute           → Script processes event, queries world, builds update_set
                    ↓
yield             → Script serializes continuation, returns update_set
                    ↓
validate          → Engine applies validation rules to update_set
                    ↓
apply or reject   → Valid updates applied; rejected scripts flagged
```

If the script encounters an unrecoverable error, it executes `exit error_code` and is permanently deregistered. Scripts that yield normally are resumed on the next matching event.

### 2.2 Input Contract (Engine → Script)

On each resume, the script receives access to:

```c
/* Provided by the engine to the VM on each resume. */
typedef struct aegis_script_input {
    const aegis_event_t *event;              /* The event being processed. */
    script_entity_view_t world;              /* Read-only entity snapshot. */
    aegis_fiber_state_t  continuation;       /* Serialized state from previous yield. */
} aegis_script_input_t;
```

**Event structure** (events use `!` prefixed topic names):
```c
typedef struct aegis_event {
    uint32_t type;           /* Event type hash (from topic name). */
    uint32_t source;         /* Entity ID that triggered event. */
    uint32_t payload_len;    /* Payload size in bytes. */
    uint32_t tick;           /* Server tick when event was emitted. */
    uint8_t  payload[];      /* Event-specific data (schema-validated). */
} aegis_event_t;
```

**World view** — the read-only entity snapshot provided by `script_entity_view_t`:
```c
/* Already defined in edit_script_env.h */
typedef struct script_entity_view {
    const script_entity_snapshot_t *entities;
    uint32_t count;
    uint32_t capacity;
} script_entity_view_t;
```

Scripts query entities via bytecode instructions that read from this snapshot. They cannot directly access entity storage — all reads go through `entity_attrs_get()` under the hood, and all reads are logged to the execution trace.

### 2.3 Output Contract (Script → Engine)

Scripts return an `aegis_update_set_t` — a list of desired state mutations:

```c
typedef struct aegis_state_update {
    uint32_t target;         /* Target entity ID. */
    uint16_t key;            /* Attribute key (SCRIPT_KEY_*). */
    uint8_t  type;           /* Attribute type (SCRIPT_ATTR_*). */
    uint8_t  size;           /* Payload size in bytes. */
    uint32_t hints;          /* Validation hint flags. */
    uint8_t  value[16];      /* Inline value (max 16 bytes = vec4/quat). */
} aegis_state_update_t;

typedef struct aegis_update_set {
    uint64_t trace_hash;     /* Content-addressable execution trace. */
    uint64_t wall_time_ns;   /* Time spent executing. */
    uint32_t count;          /* Number of updates. */
    uint32_t capacity;       /* Max updates (pre-allocated). */
    aegis_state_update_t updates[]; /* Flexible array member (last field). */
} aegis_update_set_t;
```

This maps directly to the engine's `script_env_write_attr()` for applying updates via `script_update_buffer_t`.

---

## 3. Bytecode Architecture

### 3.1 Format

- **Register-based**: 256 general-purpose registers, each 128 bits wide
- **Fixed-width instructions**: 128 bits (4 × uint32_t words)
- **Per-script arena memory**: bounds-checked on every access, reset on yield
- **Content-addressable**: statistical canonicalization enables similarity detection

**Register layout** (128 bits / 16 bytes each):
```c
typedef union aegis_register {
    int32_t   i32;
    int64_t   i64;
    float     f32;
    double    f64;
    uint32_t  u32;
    uint64_t  u64;
    float     vec2[2];
    float     vec3[3];
    float     vec4[4];       /* Also used for quaternions. */
    uint32_t  entity_id;
    uint32_t  handle;        /* Async handle, entity query handle. */
    uint8_t   bytes[16];
} aegis_register_t;
```

With 256 registers at 16 bytes each, the register file is 4 KB — fits comfortably in L1 cache.

### 3.2 Type System

```
Primitive types (fit in a single register):
  i32, i64, f32, f64    — numeric
  bool                  — uint8 0/1
  entity_id             — uint32 entity reference (matches ecs entity_t.index)

Math types (packed in register):
  vec2                  — float[2] (8 bytes)
  vec3                  — float[3] (12 bytes)
  quat                  — float[4] (16 bytes)

Attribute keys (compile-time constants, matching entity_attrs.h):
  KEY_POS       = 0     — vec3: world position
  KEY_ROT       = 1     — vec3: euler rotation (degrees)
  KEY_SCALE     = 2     — vec3: per-axis scale
  KEY_NAME      = 3     — str: display name
  KEY_TYPE      = 4     — u32: entity type ID
  KEY_BODY_IDX  = 5     — u32: physics body index
  KEY_MATERIAL  = 6     — str: material path
  KEY_ECS_BASE  = 64    — ECS component keys start here
  KEY_USER      = 256   — user-defined keys start here

Complex types (heap-allocated in script arena):
  array<T>              — fixed-size, bounds-checked
  map<K,V>              — SipHash-keyed to resist hash flooding
```

### 3.3 Instruction Set

All mnemonics are `snake_case`. All instructions are 128 bits: `[opcode:u32] [a:u32] [b:u32] [c:u32]`. Unused operand slots are zero.

**Coroutine control:**
```
yield                          — serialize state, return update_set to scheduler
resume                         — entry point marker (first instruction of script)
exit         r_code            — terminate script permanently with error code
```

**Function calls (internal to script):**
```
call         label             — push return address + frame, jump to label
ret                            — pop frame, return to caller
```

Note: `resume`/`yield` define the coroutine boundary. `call`/`ret` are for internal helper functions within a script. Max call depth: 256 frames.

**Async operations:**
```
wait         r_result, r_flag, r_handle  — poll + yield fiber if still pending
poll         r_result, r_flag, r_handle  — read async task status (non-blocking)
```

See §3.5 for detailed async task semantics.

**Event access:**
```
event_type   r_dst             — load event type hash into register
event_src    r_dst             — load source entity ID into register
event_field  r_dst, off, type  — load typed field from event payload at byte offset
                                 (bounds-checked against payload_len; type determines
                                  read size: SCRIPT_ATTR_F32=4, VEC3=12, etc.)
```

**World queries (all reads traced):**
```
query_entity   r_dst, r_entity_id   — find entity in snapshot by ID; r_dst = handle
                                       (index into snapshot array, or -1 if not found)
get_attr       r_dst, r_handle, key  — read attribute from entity snapshot by key
                                       (key is a SCRIPT_KEY_* constant; reads via
                                        entity_attrs_get() for dynamic attrs, or
                                        fixed fields for well-known keys)
entity_count   r_dst                 — number of active entities in snapshot
entity_at      r_dst, r_index        — entity handle at snapshot index
                                       (for iteration: 0..entity_count-1)
```

**Async world queries:**
```
vis_test     r_handle, r_origin, r_ray_vec   — submit async raycast; direction of
                                                r_ray_vec gives ray direction, magnitude
                                                gives max distance; returns async handle
nav_query    r_handle, r_from, r_to          — submit async nav mesh query from point
                                                to point; returns async handle
```

**State mutation building (maps to script_env_write_attr):**
```
build_update   r_dst                 — create empty update builder in register
target_entity  r_upd, r_entity_id    — set target entity for update
set_field      r_upd, key, r_val     — set attribute value (key = SCRIPT_KEY_* imm)
add_hint       r_upd, hint_type      — add validation hint flag
push_update    r_upd                 — finalize and append to update_set
```

**Data movement:**
```
mov          r_dst, r_src            — copy register
load_imm     r_dst, imm             — load 32-bit immediate (int or float) into register
load_imm64   r_dst, imm_lo, imm_hi  — load 64-bit immediate (uses both b and c slots)
```

**Arithmetic (overflow-checked for integers, IEEE 754 for floats):**
```
add    r_dst, r_a, r_b        sub    r_dst, r_a, r_b
mul    r_dst, r_a, r_b        div    r_dst, r_a, r_b
mod    r_dst, r_a, r_b        neg    r_dst, r_a
```

**Bitwise & logic:**
```
and    r_dst, r_a, r_b        or     r_dst, r_a, r_b
xor    r_dst, r_a, r_b        not    r_dst, r_a
```

**Comparison (result is bool in r_dst):**
```
eq     r_dst, r_a, r_b        ne     r_dst, r_a, r_b
lt     r_dst, r_a, r_b        le     r_dst, r_a, r_b
gt     r_dst, r_a, r_b        ge     r_dst, r_a, r_b
```

**Control flow:**
```
jmp          label             — unconditional jump
jmp_if       r_cond, label     — jump if r_cond is truthy (nonzero)
jmp_if_not   r_cond, label     — jump if r_cond is falsy (zero)
```

**Memory (per-script arena, bounds-checked on every access):**
```
alloc        r_dst, size       — bump-allocate from script arena; r_dst = offset
load         r_dst, r_base, off — load 16 bytes from arena[r_base + off]
store        r_base, off, r_val — store 16 bytes to arena[r_base + off]
```

No `free` instruction — the script arena is reset on each `yield`. Multi-yield scripts that accumulate state use registers and the continuation serializer.

**Type conversion:**
```
i32_to_f32   r_dst, r_src      f32_to_i32   r_dst, r_src
i64_to_f64   r_dst, r_src      f64_to_i64   r_dst, r_src
f64_to_f32   r_dst, r_src      f32_to_f64   r_dst, r_src
```

**Vector & quaternion math:**
```
vec3_add     r_dst, r_a, r_b   vec3_sub     r_dst, r_a, r_b
vec3_mul     r_dst, r_a, r_b   vec3_scale   r_dst, r_vec, r_scalar
vec3_dot     r_dst, r_a, r_b   vec3_cross   r_dst, r_a, r_b
vec3_len     r_dst, r_a        vec3_norm    r_dst, r_a
quat_mul     r_dst, r_a, r_b   quat_rotate  r_dst, r_quat, r_vec
```

### 3.4 Instruction Encoding

Each instruction is 128 bits (16 bytes), stored as 4 consecutive `uint32_t` words:

```
Word 0: opcode
  Bits 0-15:   instruction opcode (enum value, 65536 possible)
  Bits 16-18:  immediate mode flags
               Bit 16 = operand A is an immediate value
               Bit 17 = operand B is an immediate value
               Bit 18 = operand C is an immediate value
  Bits 19-31:  reserved (must be zero)

Word 1: operand A (uint32_t)
  Register mode: register index (0-255)
  Immediate mode: literal value (uint32 / int32 / IEEE 754 float)

Word 2: operand B (uint32_t)
  Register mode: register index (0-255)
  Immediate mode: literal value or label offset

Word 3: operand C (uint32_t)
  Register mode: register index (0-255)
  Immediate mode: literal value
```

**Example encoding of `vec3_add r2, r0, r1`:**
```
Word 0: 0x00000040  (opcode = VEC3_ADD, no immediate flags)
Word 1: 0x00000002  (A = r2, destination)
Word 2: 0x00000000  (B = r0, source)
Word 3: 0x00000001  (C = r1, source)
```

**Example encoding of `load_imm r5, 0.1` (A=register, B=immediate):**
```
Word 0: 0x00020030  (opcode = LOAD_IMM, bit 17 set = B is immediate)
Word 1: 0x00000005  (A = r5, destination)
Word 2: 0x3DCCCCCD  (B = 0.1f as IEEE 754)
Word 3: 0x00000000  (C = unused)
```

This encoding is trivial to decode (no variable-length parsing, no bit packing) and aligns naturally on modern CPUs. At 16 bytes per instruction, a 1000-instruction script is 16 KB.

### 3.5 Async Task Protocol (wait / poll)

Async operations (`vis_test`, `nav_query`) follow a lock-free producer-consumer pattern between the script fiber and the world subsystem:

**1. Submission (script fiber):**

When the VM executes an async instruction (e.g., `vis_test`):
- Allocates a result slot in the script's arena (owned by the calling fiber)
- Builds an `aegis_async_task_t` with parameters and a pointer to the result slot
- Enqueues the task into the world subsystem's **async task buffer** (lock-free MPSC ring)
- Stores a handle (task index) in the destination register

```c
typedef struct aegis_async_task {
    _Atomic uint32_t status;       /* AEGIS_ASYNC_PENDING → COMPLETE | ERROR */
    uint32_t         task_type;    /* AEGIS_TASK_VIS_TEST, AEGIS_TASK_NAV, etc. */
    void            *result_ptr;   /* Points into script arena (fiber-owned). */
    uint32_t         result_cap;   /* Pre-allocated result capacity. */
    uint8_t          params[64];   /* Task-specific input parameters. */
} aegis_async_task_t;

enum {
    AEGIS_ASYNC_PENDING  = 0,
    AEGIS_ASYNC_COMPLETE = 1,
    AEGIS_ASYNC_ERROR    = 2,
};
```

**2. Execution (world subsystem, separate fiber/thread):**

The world subsystem drains its async task buffer on each tick (or continuously) and executes the tasks without contention:
- Raycasts use the `bodies_curr` physics buffer (read-only, end of previous tick)
- Nav queries use the navigation mesh (read-only)
- On completion, writes result data to `result_ptr` and atomically sets `status`

**3. Polling (script fiber):**

`poll r_result, r_flag, r_handle`:
- Reads `status` from the task (atomic load, acquire ordering)
- If `AEGIS_ASYNC_COMPLETE`: copies result data from the arena slot into `r_result`, sets `r_flag` to status code
- If `AEGIS_ASYNC_PENDING`: sets `r_flag` to `AEGIS_ASYNC_PENDING`, `r_result` is undefined
- If `AEGIS_ASYNC_ERROR`: sets `r_flag` to error code, `r_result` may contain error details

`wait r_result, r_flag, r_handle`:
- Executes a `poll`
- If `r_flag == AEGIS_ASYNC_PENDING`: yields from the script (and therefore from the calling fiber via `job_yield()`)
- On next resume, the VM re-executes the `wait` instruction (program counter does not advance)
- If complete or error: behaves like `poll` and advances past the instruction

This ensures async queries never block the fiber scheduler and integrate cleanly with `job_dispatch` / `job_yield`.

---

## 4. Statistical Canonicalization

Bytecode is normalized to enable similarity detection and pattern matching in the security trace database.

### 4.1 Normalization Passes

**1. Alpha renormalization:**
All registers renamed by order of first definition. `r5 = r1 + r2` and `r8 = r3 + r4` become structurally identical if dataflow-equivalent.

**2. Commutative sorting:**
Commutative operations (`add`, `mul`, `and`, `or`) have operands sorted by register index. `add r1, r2, r3` and `add r1, r3, r2` become identical.

**3. Dead code elimination:**
Unreferenced computations removed. Only externally-visible effects preserved (`yield`, `push_update`, async submissions).

**4. Constant folding (limited):**
Pure arithmetic on constants evaluated at compile time. Preserves trap semantics (division by zero still trapped).

**5. Graph linearization:**
Control flow graph serialized in reverse postorder (RPO). Ensures structural equivalence regardless of source order.

### 4.2 Graph Representation

For pattern matching, bytecode is converted to a labeled property graph:

**Nodes:**
- `instruction`: bytecode operation
- `register`: SSA value
- `constant`: immediate value
- `memory`: arena allocation site
- `event`: event source
- `update`: state mutation

**Edges:**
- `def_use`: register definition to uses
- `control`: control flow (conditional, jump)
- `data`: data dependencies
- `capability`: security-relevant operations (`query_entity`, `push_update`, `vis_test`)

**Similarity hashing:**
- **MinHash**: for finding "resemblance" (scripts sharing many substructures)
- **SimHash**: for finding "near-duplicates" (scripts with small modifications)
- **Trace graph hash**: content-addressable execution trace for dynamic analysis

---

## 5. Execution Tracing

Every script execution produces a content-addressable trace graph stored in the security trace database (a planned graph database; see rpg-rxmy).

### 5.1 Trace Content

**Static capture (compile time):**
- Normalized bytecode hash
- Capability surface (which `query_entity`, `push_update`, `vis_test` operations used)
- Attribute access patterns (which `SCRIPT_KEY_*` keys read, which updated)

**Dynamic capture (runtime):**
- Control flow path taken (branch targets, loop iterations)
- Event types processed
- World query patterns (which entities accessed, access frequency)
- Update composition (which attribute keys modified, which entities targeted)
- Wall-time consumed per segment

### 5.2 Trace Graph Structure

Conceptually, each trace forms a directed graph:

```
trace(hash="a1b2c3", script_id="knockback_handler")
  → processed(event: {type: "!hit"})
  → queried(entity: {id: 42})
  → read(attr: {key: KEY_POS, value: [10.0, 5.0, 3.0]})
  → computed(op: vec3_add, inputs: [...])
  → wrote(update: {key: KEY_POS, value: [12.5, 5.0, 3.0]})
  → yielded
```

Storage format is C structs and flat arrays in the near term. The planned graph database (rpg-rxmy) will provide richer query capabilities for cross-trace analysis and exploit pattern matching.

### 5.3 Trace Analysis Queries (Planned)

Example queries for the graph database (future implementation):

**Find teleport exploits:**
- Match traces that write `KEY_POS` with delta > max_speed × tick_dt × tolerance

**Find excessive entity scanning:**
- Match traces that query > 100 distinct entities per tick

**Temporal behavior change detection:**
- Compare trace hashes over time for a given script; sudden changes flag review

---

## 6. Mandatory Yielding & Resource Enforcement

### 6.1 Per-Tick Yielding (Security-Critical)

Every script MUST yield at least once per server tick. This prevents:
- **DoS via infinite loops**: scripts cannot consume indefinite CPU
- **Scheduler starvation**: other scripts and engine systems get time
- **State inconsistency**: long-running scripts see stale world state

**Enforcement — fuel metering (primary):**
1. Each instruction decrements a fuel counter
2. `yield` resets the fuel counter
3. Out-of-fuel triggers forced yield (not error — prevents DoS)
4. Forced-yield scripts are flagged for review

**Enforcement — wall-time backstop (secondary):**
1. Engine records `clock_gettime` before `resume`
2. Every N instructions (configurable, default 256), VM checks elapsed wall-time
3. If budget exceeded (configurable, default 1 ms), force yield and flag
4. This catches cases where individual instructions are unexpectedly expensive

Fuel is the primary mechanism (deterministic, no syscall overhead). Wall-time is a safety net for pathological cases.

### 6.2 Resource Limits

| Resource | Limit | Enforcement |
|----------|-------|-------------|
| Arena memory per script | Configurable (default 64 KB) | Bounds check on `alloc` |
| Call stack depth | 256 frames | Counter on `call` |
| Fuel per yield | Configurable (default 10000) | Decremented per instruction |
| Wall-time per tick | Configurable (default 1 ms) | Checked every 256 instructions |
| Updates per yield | 1024 | Hard limit on `push_update` |
| Async tasks per yield | 16 | Hard limit on `vis_test` / `nav_query` |

---

## 7. Server-Side Validation Rules

Scripts cannot directly mutate state. All mutations pass through engine validation.

### 7.1 Flagged Attributes

Server admin marks sensitive attribute keys:
```json
{
  "flagged_attributes": {
    "0":  {"name": "position",  "fields": ["x", "y", "z"]},
    "1":  {"name": "rotation",  "fields": ["x", "y", "z"]},
    "5":  {"name": "body_index"}
  }
}
```

Keys use `SCRIPT_KEY_*` numeric values to match `entity_attrs_t`.

### 7.2 Validation Rules

Rules defined per-server in JSON (parsed at startup, compiled to C function pointers for runtime evaluation):

```json
{
  "rules": [
    {
      "name": "no_teleport",
      "condition": {"update_key": 0},
      "check": "distance_check",
      "params": {"max_speed_mult": 1.5},
      "action": "reject_and_flag"
    },
    {
      "name": "velocity_cap",
      "condition": {"update_key": 5},
      "check": "range_check",
      "params": {"max": 100.0},
      "action": "clamp"
    },
    {
      "name": "health_rate_limit",
      "condition": {"update_key": 300},
      "check": "rate_limit",
      "params": {"max_per_second": 10},
      "action": "reject_and_flag"
    }
  ]
}
```

### 7.3 Rule Execution

1. Script returns `aegis_update_set_t`
2. Engine iterates updates
3. For each flagged attribute key, applicable rules execute
4. Rule actions: `allow`, `reject`, `reject_and_flag`, `clamp`, `log`
5. Rejected updates dropped; flagged scripts logged for review

### 7.4 Validation Hints

Scripts provide metadata to help validation:
```
add_hint  r_upd, HINT_MOVEMENT     — "this is movement, not teleport"
add_hint  r_upd, HINT_AUTHORITY    — "server-authoritative calculation"
add_hint  r_upd, HINT_PREDICTION   — "client prediction, verify tolerance"
```

---

## 8. C API for Engine Integration

All types use `snake_case` with `_t` suffix. All functions use `aegis_` prefix.

```c
/* ==================== LIFECYCLE ==================== */

/* Compile source to bytecode (allocates from provided arena). */
aegis_bytecode_t *aegis_compile(const char *source, size_t len,
                                 aegis_arena_t *arena);

/* Load pre-compiled bytecode. */
aegis_bytecode_t *aegis_load(const uint8_t *data, size_t len,
                              aegis_arena_t *arena);

/* Get content hash of normalized bytecode. */
uint64_t aegis_bytecode_hash(const aegis_bytecode_t *bc);

/* ==================== EXECUTION ==================== */

/* Create script instance with isolated arena. */
aegis_script_t *aegis_script_create(const aegis_bytecode_t *bc,
                                     const aegis_config_t *config,
                                     aegis_arena_t *arena);

/* Execute script with input; writes updates to out_updates. */
aegis_result_t aegis_execute(aegis_script_t *script,
                              const aegis_script_input_t *input,
                              uint64_t fuel_budget,
                              aegis_update_set_t *out_updates);

/* Resume suspended script (after yield or wait). */
aegis_result_t aegis_resume(aegis_script_t *script,
                             const aegis_script_input_t *input,
                             uint64_t fuel_budget,
                             aegis_update_set_t *out_updates);

/* Get trace of last execution. */
const aegis_trace_t *aegis_get_trace(const aegis_script_t *script);

/* ==================== VALIDATION ==================== */

/* Load validation rules from JSON. */
bool aegis_rules_load(aegis_rule_set_t *rules, const char *json,
                       size_t json_len);

/* Check update_set against rules. */
aegis_validation_result_t aegis_validate(const aegis_rule_set_t *rules,
                                          const aegis_update_set_t *updates,
                                          const script_entity_view_t *world);

/* ==================== ASYNC TASK BUFFER ==================== */

/* Initialize the async task buffer (MPSC ring). */
bool aegis_async_buffer_init(aegis_async_buffer_t *buf, uint32_t capacity);

/* Drain completed tasks (called by world subsystem). */
uint32_t aegis_async_buffer_drain(aegis_async_buffer_t *buf,
                                   aegis_async_task_t *out_tasks,
                                   uint32_t max_tasks);
```

---

## 9. Security Model Summary

| Layer | Mechanism | Purpose |
|-------|-----------|---------|
| **Static** | Bytecode canonicalization + similarity hashing | Detect known malicious code patterns |
| **Dynamic** | Execution trace graph + database queries | Detect runtime exploit patterns |
| **Stateful** | Validation rules on update_set | Prevent invalid state mutations |
| **Resource** | Arena isolation + fuel metering + wall-time | Prevent DoS, sandbox escape |
| **Temporal** | Per-tick mandatory yielding | Ensure fairness, prevent starvation |
| **Async** | Task buffer isolation | Prevent contention with world subsystem |

---

## 10. Implementation Phases

### Phase 1: Core VM
- 128-bit register-based interpreter
- Full ISA (arithmetic, control flow, memory, data movement)
- `yield` / `resume` / `exit` with continuation serialization
- Arena memory isolation with bounds checking
- Fuel metering

### Phase 2: Entity & Event Integration
- Event type system with `!` prefixed topics
- Server-side event queue infrastructure (new engine component)
- Entity query via `script_entity_view_t` and `entity_attrs_t`
- `aegis_update_set_t` construction and `script_update_buffer_t` integration

### Phase 3: Async Operations
- `vis_test` / `nav_query` with MPSC async task buffer
- `poll` / `wait` with fiber yield integration (`job_yield`)
- World subsystem task drain on physics thread

### Phase 4: Validation
- JSON rule parser
- Rule engine with compiled check functions
- Flagged attribute tracking
- Update filtering

### Phase 5: Tracing & Canonicalization
- Trace graph construction (C struct ring buffers)
- Normalization passes (alpha, commutative, DCE, constant fold, RPO)
- Similarity hashing (SimHash, MinHash)
- Graph database export (future integration with rpg-rxmy)

### Phase 6: Hardening
- Wall-time backstop enforcement
- Async task limits
- Recursion depth enforcement
- Fuzzing and security audit

---

## 11. References

### Bytecode & VM Design
1. Ierusalimschy, R., de Figueiredo, L. H., & Celes, W. (2005). "The Implementation of Lua 5.0". *Journal of Universal Computer Science*.
2. Haas, A. et al. (2017). "Bringing the Web up to Speed with WebAssembly". *PLDI*.
3. Shi, Y. et al. (2008). "Virtual Machine Showdown: Stack Versus Registers". *ACM TACO*.

### Event-Driven & Fiber Systems
4. Gregory, J. (2018). *Game Engine Architecture* (3rd ed.). Chapter 15: Threading.
5. De Wael, M. et al. (2015). "Fork/Join Parallelism in the Wild". *OOPSLA*.

### Tracing & Provenance
6. Hassan, W. U. et al. (2020). "OmegaLog: High-Fidelity Attack Investigation via Transparent Multi-layer Log Analysis". *NDSS*.
7. Ji, Y. et al. (2022). "Enabling Refinable Cross-Host Attack Investigation with Efficient Data Flow Tagging and Tracking". *USENIX Security*.

### Graph-Based Malware Detection
8. Chen, J. et al. (2024). "Graphite: A Real-time Graph-based Method for Detecting Malicious Activity in Ethereum Smart Contracts". *arXiv:2401.07230*.
9. Chen, W. et al. (2021). "THREATTRACE: A Trace-based Threat Detection and Tracking Method via Host-based Data Provenance Graph". *IEEE Access*.

### Similarity Detection & Hashing
10. Broder, A. (1997). "On the Resemblance and Containment of Documents". *Compression and Complexity of Sequences*.
11. Charikar, M. (2002). "Similarity Estimation Techniques from Rounding Algorithms". *STOC*.

### ECS & Game Engines
12. Shacklett, B. et al. (2023). "A High-Performance, Heterogeneity-Aware ECS for Real-time Game Engines". *arXiv:2310.02614*.

### Security & Sandboxing
13. Wahbe, R. et al. (1993). "Efficient Software-Based Fault Isolation". *SOSP*.
14. Sehr, D. et al. (2010). "Adapting Software Fault Isolation to Contemporary CPU Architectures". *USENIX Security*.
15. Aumasson, J.-P. & Bernstein, D. J. (2012). "SipHash: a fast short-input PRF". *INDOCRYPT*.

---

## Appendix A: Example Bytecode

### A.1 Knockback on Hit

Subscribes to `!hit`, reads impact direction and force from the event payload, computes a knockback displacement using vector math, and moves the entity with a `HINT_MOVEMENT` validation hint.

```asm
.topic !hit

resume:
    ; load event data
    event_src       r0                          ; r0 = hit entity ID
    event_field     r1, 0, SCRIPT_ATTR_VEC3     ; r1 = hit direction (vec3, bytes 0-11)
    event_field     r2, 12, SCRIPT_ATTR_F32     ; r2 = hit force (f32, bytes 12-15)

    ; get entity's current position from snapshot
    query_entity    r3, r0                      ; r3 = entity handle
    get_attr        r4, r3, KEY_POS             ; r4 = current position (vec3)

    ; compute knockback: pos + normalize(dir) * force * 0.016
    vec3_norm       r5, r1                      ; r5 = normalized hit direction
    load_imm        r6, 0.016                   ; r6 = delta_time (approx 1/60)
    mul             r7, r2, r6                  ; r7 = force * dt
    vec3_scale      r8, r5, r7                  ; r8 = displacement vector
    vec3_add        r9, r4, r8                  ; r9 = new position

    ; build update with movement hint
    build_update    r10
    target_entity   r10, r0
    set_field       r10, KEY_POS, r9
    add_hint        r10, HINT_MOVEMENT
    push_update     r10

    yield
```

### A.2 AI Behavior with Async Queries

Subscribes to `!behave` (AI tick event), performs an async raycast to check line-of-sight, then an async nav query to find a path. Uses `wait` to yield the fiber while queries execute.

```asm
.topic !behave

resume:
    ; load NPC entity and its state
    event_src       r0                          ; r0 = NPC entity ID
    query_entity    r1, r0
    get_attr        r2, r1, KEY_POS             ; r2 = NPC position (vec3)
    get_attr        r3, r1, KEY_ROT             ; r3 = NPC rotation (vec3 euler)

    ; load patrol target from user attribute
    load_imm        r4, 260                     ; r4 = KEY_USER + 4 (patrol_target key)
    get_attr        r5, r1, r4                  ; r5 = patrol target entity ID

    ; get target position
    query_entity    r6, r5
    get_attr        r7, r6, KEY_POS             ; r7 = target position (vec3)

    ; compute ray from NPC to target
    vec3_sub        r8, r7, r2                  ; r8 = direction vector (also encodes distance)

    ; submit async raycast (magnitude of r8 = max distance)
    vis_test        r9, r2, r8                  ; r9 = async handle

    ; yield fiber until raycast completes
    wait            r10, r11, r9                ; r10 = hit result, r11 = status flag

    ; check if path is clear (r11 == AEGIS_ASYNC_COMPLETE and no hit)
    load_imm        r12, AEGIS_ASYNC_ERROR
    eq              r13, r11, r12
    jmp_if          r13, handle_error

    ; submit async nav query: find path from NPC to target
    nav_query       r14, r2, r7                 ; r14 = nav async handle

    ; yield fiber until nav query completes
    wait            r15, r16, r14               ; r15 = first waypoint, r16 = status

    ; move toward first waypoint
    vec3_sub        r17, r15, r2                ; r17 = direction to waypoint
    vec3_norm       r18, r17                    ; r18 = normalized direction
    load_imm        r19, 5.0                    ; r19 = move speed
    load_imm        r20, 0.016                  ; r20 = dt
    mul             r21, r19, r20               ; r21 = step distance
    vec3_scale      r22, r18, r21              ; r22 = step vector
    vec3_add        r23, r2, r22                ; r23 = new position

    ; build position update
    build_update    r24
    target_entity   r24, r0
    set_field       r24, KEY_POS, r23
    add_hint        r24, HINT_MOVEMENT
    push_update     r24

    yield

handle_error:
    ; log error and exit (script is no longer valid)
    load_imm        r30, 1                      ; error code 1 = vis_test failed
    exit            r30
```

---

*End of Specification*
