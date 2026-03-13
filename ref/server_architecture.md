# Server Architecture

## Overview

The server is a headless authoritative simulation that:

- drains inbound player input via a global inbound topic
- processes high-level events through the entity net pump
- updates gameplay / controllers (primarily kinematic)
- performs a full physics tick via the physics runner
- broadcasts **reliable events** (spawn/despawn/death/mesh data) via per-client reliable outbound topics
- broadcasts high-rate **unreliable state** (rigid body movement) via per-client unreliable outbound topics
- supports velocity-proportional priority body updates for constrained (jointed) bodies

The design is biased for **encapsulation and simplicity**:
- reliable delivery concerns are isolated to one module (reliable stream per client fiber)
- unreliable state delivery is plain datagrams (no stream HOL semantics)
- network sockets live on a dedicated I/O thread; client fibers produce already-encoded packets
- per-client fiber handles all RUDP framing and outbound pumping

Source docs:
- `ref/server_tick_callgraph.md` -- call graph + DFD
- `ref/physics_tick_callgraph.md` -- physics pipeline
- `ref/networking_runtime.md` -- transport layer details

---

## Subsystems and Reuse

| Concern | Module(s) | Notes |
|---|---|---|
| Physics simulation | `src/physics/world/phys_tick_runner.c`, `tick_parallel.c` | dedicated pthread + parallel tick |
| Reliable transport | `src/net/rudp/stream/stream_io.c` + RUDP peer | reliable stream frames over UDP (and later TCP) |
| Unreliable transport | `net_udp_socket_sendto/recvfrom` | raw datagrams, MTU-sized |
| Server net runtime | `src/server/net/runtime/` | per-client fiber jobs, demux pump, inbox routing |
| Entity net pump | `src/server/entity/net/pump.c` | JOIN/WELCOME handling, event dispatch |
| Replication encode | `src/server/repl/repl_server_tick.c` | event encoder + state encoder loops |
| Body state broadcast | `src/server/physics/net/body_state_broadcast.c` | scans physics world, enqueues per-client updates |
| Priority body sender | `src/server/physics/net/priority_body_sender.c` | velocity-proportional constrained body updates |
| Pre-physics sync | `src/server/physics/sync/pre_physics_sync.c` | parallel ECS->physics write pass |
| Tick loop | `src/server/tick/tick_loop.c` | fixed-timestep accumulator with catch-up cap |
| Tick encoder | `src/server/tick/tick_encoder.c` | per-client event + state encoding to topics |
| ECS | `src/ecs/world.c` | authoritative game objects |
| Job system | `src/job/*` | looping encoder jobs + physics stage dispatch |

---

## Transport Model

### Reliable stream (events + initial world state)

Reliable messaging uses a stream abstraction so we can later swap transports (e.g. TCP)
without touching gameplay/replication code.

**Properties required:**
- ordered delivery
- retransmission + ACK handling (when over UDP)
- message reconstruction + in-order sorting
- backpressure with explicit, observable drop/fail behavior

**Current wire format when over UDP:**
- RUDP packet schema: `NET_REPL_SCHEMA_STREAM_FRAME` (0x200C)
- stream frame bytes: `[seq:u16 LE][chan:u16 LE][payload...]`
- payload bytes: `[schema_id:u16 LE][payload...]`

**Stream configuration in client fibers:**
- 1 reliable channel, 512 slot window (large for spawn bursts)
- max payload size: 512 bytes (`NET_RUDP_MAX_PACKET_SIZE`)

**Note:** head-of-line blocking is acceptable because reliable traffic volume is low
(spawns/events are buffered and batched).

### Unreliable state (movement)

Rigid body movement is sent as RUDP unreliable packets (schema-prefixed datagrams
wrapped in RUDP framing for ACK piggyback).  Most outbound traffic is in this category.

**Datagram layout inside RUDP frame:**

```
[schema_id : u16 LE] [payload : bytes]
```

Schemas:
- `NET_REPL_SCHEMA_BODY_STATE` (0x200B) -- quantized movement (44 bytes)
- `NET_REPL_SCHEMA_BODY_STATE_BATCH` (0x200E) -- batched body states (up to 11 per batch)
- `NET_REPL_SCHEMA_STATE_CUBE` (0x2003) -- legacy full-float state cube (40 bytes)
- `NET_REPL_SCHEMA_STATE_CUBE_BATCH` (0x2006) -- batched state cubes

**Demux rule on receive (client fiber):**
- if first 4 bytes match `NET_RUDP_PROTOCOL_ID_P008` (0x52555038, 'RUP8') -> feed RUDP peer
- else -> treat as raw unreliable datagram and decode `schema_id` directly

---

## Server Network Runtime (`fr_server_net_runtime_t`)

### Architecture

The runtime manages all client connections and their networking fibers:

1. **Demux pump** (`fr_server_net_runtime_pump()`):
   - Called from the I/O thread or main loop.
   - Receives UDP datagrams and routes to per-client inboxes by transport key
     (FNV1a hash of sockaddr bytes).
   - For JOIN packets from unknown addresses, allocates a new client slot.
   - One-packet staging area per client until the fiber publishes its stack inbox pointer.

2. **Per-client fibers** (job system scheduled):
   - Each client gets a fiber running `fr_server_client_fiber_main()`.
   - Fiber owns: RUDP peer state, outbound stream (1 channel, 512 slots),
     and local inbox (64 slots x 512 bytes on fiber stack).
   - Loop: pop inbox -> demux RUDP/raw -> publish to global inbound topic ->
     drain reliable outbound topic -> flush stream -> drain unreliable outbound topic ->
     RUDP resend -> yield.

3. **Per-client outbound topics**:
   - `out_reliable`: gameplay systems push `[schema_id:u16 LE][payload]` messages.
   - `out_unreliable`: body state broadcaster pushes encoded updates.
   - Accessed via `fr_server_net_runtime_client_out_topics()`.

### Client State (`fr_server_client_t`)

```
active: u8                           -- connection active flag
addr: net_udp_addr_t                 -- remote UDP address
auth_client_nonce: u32               -- mock auth identity (JOIN nonce)
transport_key: u64                   -- FNV1a hash for packet routing
inbox_ptr: atomic uintptr_t          -- points to fiber-stack inbox
pending_used: atomic bool            -- one-packet staging area
pending_size: u16 + pending_packet   -- first packet before fiber ready
out_reliable: fr_topic_channel_t*    -- reliable outbound topic
out_unreliable: fr_topic_channel_t*  -- unreliable outbound topic
send_slots: net_rudp_send_slot_t*    -- pre-allocated RUDP send storage
stop: atomic bool                    -- shutdown signal
now_ms: atomic u64                   -- current time for fiber
```

### Runtime Statistics

Atomic counters: `packets_in`, `packets_out`, `bytes_in`, `bytes_out`.

---

## Entity Net Pump (`fr_server_entity_net_pump_t`)

Consumes decoded inbound messages from the global inbound topic and produces:
- High-level events on `player_event_topic` and `entity_event_topic`.
- Outbound replication messages on per-client reliable/unreliable topics.

**JOIN handling:**
1. Receives decoded JOIN message from inbound topic.
2. Publishes `FR_SERVER_EVT_PLAYER_JOIN` to player event topic.
3. Sends WELCOME on the joining client's reliable outbound topic.
4. Sends BODY_SPAWN for existing entities.

**Input handling:**
Publishes `FR_SERVER_EVT_ENTITY_INPUT_ROT` / `INPUT_MOVE` / `INPUT_SPAWN` events.

---

## Identity Linking: ECS <-> Physics

Avoid external mapping tables as a primary truth. Instead:

- Each ECS entity that has a physics body stores `body_id` (component).
- Each physics body stores `entity_index` (u32 index into ECS entity pool).

This makes the link explicit and reduces "mystery lookups" during debugging.
A small cache (e.g. `entity_by_body_id[]`) is allowed as a derived convenience,
not the canonical source of truth.

---

## Tick Structure

The server tick loop (`fr_server_tick_loop_t`) is a stack-allocatable fixed-timestep
accumulator.  Each call to `fr_server_tick_loop_step(elapsed_us)` runs 0..N ticks
based on the accumulator and catch-up cap (discards excess to prevent spiral of death).

Each tick invokes four callbacks in order:

```
+-----------------------------------------------------------------+
|                         SERVER TICK                               |
|                                                                   |
|  1. on_drain   - Drain inbound messages        (main fiber)       |
|  2. on_physics - Kick physics simulation        (physics runner)  |
|  3. on_encode  - Encode replication data        (encoder jobs)    |
|  4. on_flush   - Flush outbound to network I/O  (encoder jobs)   |
+-----------------------------------------------------------------+
```

### Stage 1 -- Drain inbound messages

Drain `inbound_topic` produced by the network runtime. Layout is formalized by:
- `include/ferrum/server/net/inbound_message.h`

**Inbound topic layout:**

```
[client_id:u16 LE] [schema_id:u16 LE] [flags:u8] [reserved:u8] [payload...]
flags bit0 = reliable
```

The entity net pump processes these messages, dispatching high-level events and
enqueuing outbound responses.

### Stage 2 -- Physics (via physics runner)

Physics runs on a dedicated pthread (`phys_tick_runner`) with its own pacing
(nanosleep to fixed_dt).  Overload detection uses a 64-tick rolling history with
10% tolerance and hysteretic thresholds (48/64 to enter variable-dt, 6/8 recent
clean to exit).  The main tick yields until the runner's `completed_ticks` advances.

Pre-physics sync pass (`phys_pre_physics_sync` / `phys_pre_physics_sync_par`):
- Iterates dirty sync records in parallel batches.
- Writes kinematic intent (`linear_vel`, `position`, `entity_index`) into
  `bodies_next[]` via the body pool.
- Each record maps to a unique body_index, so parallel writes need no locking.

### Stage 3 -- Encode replication

The tick encoder (`fr_server_tick_encoder_t`) iterates all active clients and invokes:
- `encode_events` callback -> pushes to per-client reliable topic.
- `encode_state` callback -> pushes to per-client unreliable topic.

**Body state broadcast** (`fr_server_body_state_broadcast_t`):
- Scans active physics bodies from `phys_world_t`.
- Encodes `NET_REPL_SCHEMA_BODY_STATE` messages.
- Pushes to each connected client's unreliable outbound topic.

**Priority body sender** (`fr_priority_body_sender_t`):
- Velocity-proportional update rate for constrained bodies (joints).
- Fast-moving bodies get near-physics-rate updates; slow/sleeping bodies get nothing.
- Joint pair promotion: bodies sharing a joint are promoted to the faster partner's rate.
- Sends raw UDP BODY_STATE datagrams directly (bypasses topic system).
- Configurable: `speed_full_rate`, `speed_min`, `max_interval`.

**Reliable event batch:**
```
[NET_REPL_SCHEMA_EVENT:u16] [count:u16] [server_tick:u16]
then per entry:
  [event_type:u8] [reserved:u8] [entity_key:u64] [payload_size:u16] [payload...]
```
Event types: SPAWN (1), DESPAWN (2), DEATH (3).

### Stage 4 -- Flush outbound

Client fibers continuously drain their outbound topics and transmit.
The flush stage ensures any encoder output from this tick is available
before the next tick begins.

---

## Replication Server (`server_repl_server_t`)

The `server_repl_server_t` module provides a higher-level multi-client replication
server for direct integration (used in demos and the p008 integration):

- Manages per-client RUDP peers with caller-provided send slot storage.
- Supports an entity pose callback (`server_repl_get_entity_pose_fn`) for reading
  real positions from physics (replaces default placeholder motion).
- Pre-allocatable storage: client state, entity state, send job contexts, RUDP send slots.
- `server_repl_server_pump()`: receives and decodes packets, handles JOIN/WELCOME.
- `server_repl_server_tick()`: encodes and sends state updates for all clients.
- Statistics: `packets_sent/recv`, `bytes_sent/recv`, `state_jobs_scheduled`,
  `net_io_ns_total`, `state_update_ns_total`.
- Debug helpers for tests: force client joined, add entities, force entity known,
  schedule individual state jobs.

---

## Network I/O Thread

Network sockets should not run on job workers. Instead:

- One dedicated I/O thread owns the UDP socket(s) (and TCP listener/client sockets when needed).
- When built with `FR_NET_EMULATION` (`make EMU=1`), outbound `sendto` calls
  route through the in-process net_emulator delay queue, configured via
  engine settings before launch. See `ref/networking_callgraph.md`.
- The I/O thread:
  - receives UDP
  - calls `fr_server_net_runtime_pump()` to route packets to per-client inboxes
  - client fibers (on job workers) drain inboxes and push to outbound topics
  - client fibers call `sendto` (via callback or socket) for outbound traffic

---

## Failure Modes / Pitfalls

- **Backpressure:** topic channels have explicit backpressure policy (fail/drop-oldest/drop-newest)
  and drop counters via `fr_topic_channel_stat_dropped()`.
- **Identity drift:** ensure `body.entity_index` and `entity.body_id` are updated together;
  despawn must clear both.
- **Spawn/state ordering:** clients must process reliable spawns before applying state for that body.
- **Protocol demux:** RUDP protocol ID (big-endian u32 at offset 0) must not collide with any
  schema_id (little-endian u16); keep demux rule explicit.
- **Thread ownership:** only the I/O thread calls recvfrom; only client fibers call sendto
  (via the runtime's callback bridge); only physics runner mutates `phys_world_t` outside the pre-pass.
- **Transport key collisions:** FNV1a hash of sockaddr may collide at high client counts;
  JOIN packets fall back to nonce-based lookup. Non-JOIN collisions would misroute packets.
- **Fiber stack sizing:** client fibers stack-allocate ~68KB (inbox 64x512 + packet buffers +
  stream context). Minimum fiber stack: 256KB.
- **Stream channel full:** if the outbound stream is full, the message is pushed back to the
  topic channel and retried next tick.
