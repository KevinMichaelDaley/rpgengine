# Server Architecture

## Overview

The server is a headless authoritative simulation that:

- drains inbound player input
- updates gameplay / controllers (primarily kinematic)
- performs a full physics tick via the physics runner
- broadcasts **reliable events** (spawn/despawn/death/…) via a **reliable stream abstraction**
- broadcasts high-rate **unreliable state** (rigid body movement) as raw UDP packets (MTU-sized)

The design is biased for **encapsulation and simplicity**:
- reliable delivery concerns are isolated to one module (reliable stream)
- unreliable state delivery is plain datagrams (no queues-of-queues / no stream HOL semantics)
- network sockets live on a dedicated I/O thread; jobs produce already-encoded packets

Source docs:
- `ref/server_tick_callgraph.md` — call graph + DFD
- `ref/physics_tick_callgraph.md` — physics pipeline

### EXISTS vs PLANNED

These docs describe the **target architecture**.
Some pieces exist today (server runtime fibers, RUDP peer, stream framing, physics runner),
but the I/O-thread + encoder-job layout is the intended end state.

---

## Subsystems and Reuse

| Concern | Module(s) | Notes |
|---|---|---|
| Physics simulation | `src/physics/world/phys_tick_runner.c`, `tick_parallel.c` | runner fiber + parallel tick |
| Reliable transport | `src/net/rudp/stream/stream_io.c` + RUDP peer | reliable stream frames over UDP (and later TCP) |
| Unreliable transport | `net_udp_socket_sendto/recvfrom` | raw datagrams, MTU-sized |
| Replication encode | `src/server/repl/repl_server_tick.c` | reused; split into “event encoder” + “state encoder” loops |
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

**Note:** head-of-line blocking is acceptable because reliable traffic volume is low
(spawns/events are buffered and batched).

### Unreliable state (movement)

Rigid body movement is sent as **plain UDP packets** (not stream-framed).
Most outbound traffic is in this category.

**Datagram layout:**

```
[schema_id : u16 LE] [payload : bytes]
```

Schemas:
- `NET_REPL_SCHEMA_BODY_STATE` (0x200B) — quantized movement
- `NET_REPL_SCHEMA_STATE_CUBE` (0x2003) — optional full-float for T0/T1

**Demux rule on receive:**
- if packet begins with the RUDP protocol header/magic → feed RUDP peer
- else → treat as raw unreliable datagram and decode `schema_id` directly

---

## Identity Linking: ECS ↔ Physics

Avoid external mapping tables as a primary truth. Instead:

- Each ECS entity that has a physics body stores `body_id` (component).
- Each physics body stores `entity_index` (u32 index into ECS entity pool).

This makes the link explicit and reduces “mystery lookups” during debugging.
A small cache (e.g. `entity_by_body_id[]`) is allowed as a derived convenience,
not the canonical source of truth.

---

## Tick Structure (Target)

Each server tick runs these stages in order:

```
┌──────────────────────────────────────────────────────────────────┐
│                           SERVER TICK                            │
│                                                                  │
│  1. Drain inbound messages                (main fiber)            │
│  2. Apply controllers + gameplay          (main fiber)            │
│  3. Pre-physics ECS→Physics sync          (parallel jobs)         │
│  4. Kick physics tick                     (physics runner fiber) │
│  5. Wait physics barrier                  (main fiber yields)     │
│  6. Post-physics observe (read-only)      (main fiber)            │
│  7. Broadcast reliable event batch        (encoder jobs → I/O)    │
│  8. Broadcast unreliable body state       (encoder jobs → I/O)    │
└──────────────────────────────────────────────────────────────────┘
```

### Stage 1 — Drain inbound messages

Drain `inbound_topic` produced by the network runtime. Layout is formalized by:
- `include/ferrum/server/net/inbound_message.h`

**Inbound topic layout:**

```
[client_id:u16 LE] [schema_id:u16 LE] [flags:u8] [reserved:u8] [payload...]
flags bit0 = reliable
```

### Stage 2 — Apply controllers + gameplay

- Apply input to kinematic controllers.
- Produce gameplay events (damage, death, inventory, status, despawn).
- Enqueue reliable `server_event_t` records into a per-tick event queue.

### Stage 3 — Pre-physics ECS→Physics sync (parallel)

A parallel pre-pass writes the authoritative “physics backbuffer” for kinematic bodies:

- copy/sum controller intent → `phys_body_next.linear_vel` (or kinematic target)
- apply gameplay-driven teleports / impulses
- write `phys_body_next.entity_index` links

This stage runs as jobs (batch by body_id ranges) and completes before the
physics barrier.

### Stage 4/5 — Physics runner + barrier

Physics runs in the `phys_tick_runner` fiber and updates the world.
The main tick yields until the runner’s `completed_ticks` advances.

### Stage 6 — Post-physics observe (read-only)

After the buffer swap, ECS reads **previous** physics state until the next tick’s
pre-pass. The observed staleness is bounded by ~2–4 ms and is acceptable.

### Stage 7 — Reliable event broadcast (batched)

Reliable events are encoded into stream messages:

```
[NET_REPL_SCHEMA_EVENT:u16] [event_type:u8] [entity_key:u64] [payload...]
```

Notes:
- `entity_key` is a packed, stable handle (do not serialize `entity_t` raw).
- events are batched per tick (array of `server_event_t`).
- initial world state (“join snapshot”) should be sent as a **batched spawn burst** over the reliable stream
  (and may use TCP later without changing the framing above).

### Stage 8 — Unreliable body state broadcast (high-rate)

Encoder jobs produce final datagrams (already formatted for `sendto`).
Quantization/frequency can be tier-dependent; the wire format already supports tier bits
in `net_repl_body_state_t.flags`.

---

## Network I/O Thread (Target)

Network sockets should not run on job workers. Instead:

- One dedicated I/O thread owns the UDP socket(s) (and TCP listener/client sockets when needed).
- The I/O thread:
  - receives UDP
  - demuxes reliable-stream frames vs raw unreliable datagrams
  - pushes decoded messages to `inbound_topic` (server) / inbox queues (client)
  - drains encoder-produced TX queues and calls `sendto`

Encoder jobs run continuously and are responsible for producing already-encoded bytes.

---

## Failure Modes / Pitfalls

- **Backpressure:** topic channels and encoder queues must have explicit policy (fail vs drop-oldest) and counters.
- **Identity drift:** ensure `body.entity_index` and `entity.body_id` are updated together; despawn must clear both.
- **Spawn/state ordering:** clients must process reliable spawns before applying state for that body.
- **Protocol demux:** raw UDP schema header must not collide with RUDP magic/header; keep demux rule explicit.
- **Thread ownership:** only the I/O thread touches sockets; only physics runner mutates `phys_world_t` outside the pre-pass.
