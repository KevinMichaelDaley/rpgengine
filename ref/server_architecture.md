# Server Architecture

## Overview

The game server is a headless process that, given an already-open game world,
runs the authoritative simulation loop. It reuses existing modules throughout:

| Concern                  | Module                                      | Transport       |
|--------------------------|---------------------------------------------|-----------------|
| Physics simulation       | `phys_tick_runner` + `phys_world_tick_parallel` | (internal)    |
| Entity spawns            | `repl_server_tick` → RUDP stream            | Reliable        |
| Rigid body state         | `repl_server_tick` → RUDP unreliable        | Unreliable      |
| Player / character input | Inbound topic → `phys_cmd_drain`            | Reliable (in)   |
| Player corrections       | Correction topic → tick runner              | (internal)      |
| Job parallelism          | `job_system_t` (fiber dispatch)             | (internal)      |

## Assumptions

- The `phys_world_t` and `ecs_world_t` are already initialized and populated
  before the loop begins (world loading is out of scope here).
- All connected clients already have a `runtime_client_fiber` running.
- The server is authoritative: clients send input, server sends state.

---

## Tick Structure

Each server tick executes the following stages **in order**.  Stages that can
overlap are noted; everything else is sequential on the main fiber.

```
┌─────────────────────────────────────────────────────────┐
│                    SERVER TICK                           │
│                                                         │
│  1. Drain inbound messages         (main fiber)         │
│  2. Apply player / controller      (main fiber)         │
│  3. Kick physics tick              (physics fiber)      │
│  4. Wait for physics barrier       (main fiber yields)  │
│  5. Encode & broadcast spawns      (main fiber)         │
│  6. Encode & broadcast body state  (main fiber + jobs)  │
│  7. Flush reliable streams         (client fibers)      │
└─────────────────────────────────────────────────────────┘
```

### Stage 1 — Drain Inbound Messages

Pop all pending messages from the server's `inbound_topic` channel.
Each message carries a `schema_id` prefix:

| Schema               | Action                                      |
|----------------------|---------------------------------------------|
| `INPUT_MOVE` (0x2008)| Queue kinematic velocity for the player body |
| `INPUT_ROT`  (0x2007)| Update player look direction                 |
| `INPUT_SPAWN`(0x2009)| Enqueue a spawn command on the physics cmd topic |

Messages are decoded by `publish_inbound_()` in `runtime_client_fiber.c` and
dispatched via `fr_topic_channel_pop()`.

### Stage 2 — Apply Player / Character Controllers

For each connected player:

1. Read the latest `INPUT_MOVE` + `INPUT_ROT` received in Stage 1.
2. Compute the desired kinematic velocity from the move vector and look
   direction (server-authoritative; client prediction is cosmetic only).
3. Write the velocity into `phys_game_state_t.players[]` so the physics
   tick can integrate it.

Controllers are **kinematic for now** — the physics world treats player bodies
as kinematic objects that push dynamic bodies but are not pushed back.  This
may change to full dynamic + response later.

### Stage 3 — Kick Physics Tick

The physics tick runner (`phys_tick_runner`) is a persistent fiber:

```c
phys_tick_runner_init(&runner, world, jobs,
                      cmd_channel,        /* spawn commands in  */
                      correction_channel, /* corrections in     */
                      spawn_cb, spawn_cb_user);
phys_tick_runner_start(&runner);
```

Each iteration of the runner fiber:

1. **Paces** at fixed `dt` (yields between ticks via `job_yield()`).
2. **Drains spawn commands** from `cmd_channel` via `phys_cmd_drain()`.
3. **Runs** `phys_world_tick_parallel(world, game_state, jobs)`:
   - Tier classify, spatial update, broadphase (once per tick)
   - Narrowphase, manifold build, stabilization, constraint build,
     TGS solve, integrate (per substep)
   - TGS uses split impulse via `pseudo_velocities[]`
4. **Drains corrections** from `correction_channel`.
5. **Increments** `completed_ticks` atomic counter.

The main fiber detects tick completion by polling `completed_ticks` and
yielding until it advances.

### Stage 4 — Wait for Physics Barrier

The main fiber yields (via `job_yield()`) until
`phys_tick_runner_tick_id(&runner)` has advanced past the current tick.
This guarantees that body positions are up-to-date before replication
reads them.

### Stage 5 — Encode & Broadcast Reliable Events

All events that **must** arrive are sent through a single reliable event
system.  Each event is tagged with a type enum and carries a type-specific
payload via a union:

```c
/** Event types sent reliably to clients. */
typedef enum server_event_type {
    SERVER_EVENT_SPAWN,        /* Entity/body entered the world            */
    SERVER_EVENT_DESPAWN,      /* Entity/body removed from the world       */
    SERVER_EVENT_DEATH,        /* Entity died (may trigger client FX/UI)   */
    SERVER_EVENT_HEALTH,       /* HP changed (damage, heal)                */
    SERVER_EVENT_STATUS,       /* Status effect applied/removed            */
    SERVER_EVENT_INVENTORY,    /* Inventory mutation                       */
    SERVER_EVENT_COUNT
} server_event_type_t;

/** Union payload — sized by largest variant. */
typedef struct server_event {
    server_event_type_t type;
    entity_t            entity;   /* Which entity this event concerns */
    union {
        net_repl_body_spawn_t   spawn;    /* position, shape, flags      */
        struct { uint16_t body_id; }                          despawn;
        struct { uint16_t body_id; uint8_t cause; }           death;
        struct { int16_t delta; int16_t current; }            health;
        struct { uint8_t effect_id; bool applied; }           status;
        struct { uint16_t slot; uint16_t item_id; int16_t qty; } inventory;
    } data;
} server_event_t;
```

The enum and union live in a single header
(`include/ferrum/server/event.h`).  Adding a new reliable event type
means adding an enum value and a union variant — no new transport code.

**Broadcast path (unchanged transport):**

```
server_event_queue_push(&eq, &event)
  → server_event_encode(&event, buf, &len)
  → fr_topic_channel_push(client->out_reliable, [EVENT schema | payload])
     ↓ (client fiber pumps)
  → fr_rudp_stream_send(stream, channel=0, data, len)
  → fr_rudp_stream_flush_send(stream, stream_sendto_, user)
     ↓
  → net_rudp_peer_send_reliable_via(peer, STREAM_FRAME, frame)
```

Events are accumulated into a per-tick queue (`server_event_queue_t`,
a simple ring or arena-backed array) during stages 1–4.  At stage 5
the queue is drained and broadcast to each client.  The existing RUDP
stream provides ordered, retransmitted delivery — individual event types
do not need their own reliability logic.

**Wire format:**

```
[event_type : u8] [entity_id : u32] [payload : variable]
```

Payload encoding is type-specific and uses fixed-size fields (no
variable-length data in the initial set).  The client decodes by
switching on `event_type` and reading the corresponding fixed-size
struct.

**Spawn** and **despawn** replace the previous spawn-only path.
`NET_REPL_SCHEMA_SPAWN_BATCH` (0x2005) and `NET_REPL_SCHEMA_BODY_SPAWN`
(0x200A) are retained as wire schema IDs for backward compatibility;
a new `NET_REPL_SCHEMA_EVENT` schema ID wraps the generic event format.

### Stage 6 — Encode & Broadcast Body State (Unreliable, Tiered)

For every active rigid body, the server encodes a state snapshot and sends
it **unreliably** via direct UDP:

```
server_repl_send_some_states(rt, client_id, ...)
  → send_state_job()
  → net_repl_body_state_encode(body, &msg)
  → fr_topic_channel_push(client->out_unreliable, [BODY_STATE | payload])
     ↓ (client fiber pumps)
  → net_rudp_peer_send_unreliable_via(peer, BODY_STATE, payload)
```

**Tier-based fidelity:**

| Tier | Quantization | Send Frequency      | Notes                     |
|------|-------------|----------------------|---------------------------|
| T0   | Full float  | Every tick           | Player's own body         |
| T1   | Full float  | Every tick           | Near-field, high fidelity |
| T2   | mm + mrad/s | Every 2nd tick       | Mid-field                 |
| T3   | mm + mrad/s | Every 4th tick       | Far-field                 |
| T4   | mm + mrad/s | Every 8th tick       | Distant, sleep-eligible   |

The `net_repl_body_state_t` wire format already quantizes position to mm
and angular velocity to mrad/s (16-bit each); for T0/T1 the server can
use the full-precision `STATE_CUBE` schema (0x2003) with unquantized floats.

State updates are fire-and-forget: packet loss is acceptable because the
next tick sends a fresh snapshot.  The client interpolates between received
snapshots.

### Stage 7 — Flush Reliable Streams

Each client's `runtime_client_fiber` continuously:

1. Pops from `out_reliable` topic → pushes into `fr_rudp_stream_send()`.
2. Calls `fr_rudp_stream_flush_send()` which serializes frames as
   `[seq:u16][chan:u16][payload]` and hands them to
   `net_rudp_peer_send_reliable_via()`.
3. Retransmission is handled by `net_rudp_peer_tick_resend_via()` on each
   pump cycle — lost frames are automatically retransmitted.

The flush uses **peek-then-commit** semantics: a frame is only dequeued from
the stream's outbound ring after the underlying RUDP send succeeds.  Under
backpressure (send slots full), frames remain queued and retry next pump.

---

## Threading Model

```
  ┌──────────────────┐
  │  Main fiber      │  Tick orchestration: drain input → kick physics
  │  (job system)    │  → wait barrier → replication encode
  └──────────────────┘
           │
  ┌──────────────────┐
  │  Physics fiber   │  phys_tick_runner: paces at fixed dt, runs
  │  (job system)    │  phys_world_tick_parallel → dispatches N worker jobs
  └──────────────────┘
           │
  ┌──────────────────┐
  │  Client fiber ×N │  Per-client: pump reliable/unreliable topics → RUDP
  │  (job system)    │  Flush streams, handle retransmission
  └──────────────────┘
           │
  ┌──────────────────┐
  │  Worker threads  │  Job system worker pool: execute physics stages,
  │  (job system)    │  state encode jobs, narrowphase batches, etc.
  └──────────────────┘
```

All fibers run on the same `job_system_t` worker pool.  There are no
dedicated OS threads beyond the job system workers.  Fibers yield
cooperatively via `job_yield()`.

---

## Topic Channels (Inter-Fiber Communication)

```
  Inbound (client → server):
    client fiber → inbound_topic → main fiber

  Outbound reliable (server → client):
    main fiber → client.out_reliable → client fiber → RUDP stream

  Outbound unreliable (server → client):
    main fiber → client.out_unreliable → client fiber → RUDP direct

  Physics commands (main → physics):
    main fiber → cmd_channel → physics fiber

  Physics corrections (main → physics):
    main fiber → correction_channel → physics fiber
```

Each `fr_topic_channel_t` is a lock-free ring buffer with configurable
backpressure policy (`DROP_OLDEST`, `DROP_NEWEST`, or `FAIL`).

---

## Existing Modules Reused

| File                                          | Role in loop                           |
|-----------------------------------------------|----------------------------------------|
| `src/physics/world/phys_tick_runner.c`        | Persistent physics fiber               |
| `src/physics/world/tick_parallel.c`           | Parallel physics pipeline              |
| `src/server/repl/repl_server_tick.c`          | Spawn/state encode + dispatch          |
| `src/server/net/runtime/runtime_client_fiber.c` | Per-client reliable/unreliable pump |
| `src/server/net/runtime/runtime_pump.c`       | Top-level event pump                   |
| `src/net/rudp/stream/stream_io.c`             | Reliable stream framing                |
| `src/net/rudp/send.c`, `send_via.c`           | RUDP reliable + unreliable send        |
| `src/net/topic_channel.c`                     | Inter-fiber message passing            |
| `src/job/dispatch.c`, `fiber.c`               | Cooperative fiber job system           |
| `src/ecs/world.c`                             | Entity registry                        |
| `include/ferrum/net/replication/common.h`     | Schema IDs and wire formats            |

---

## What Needs to Be Built

The following pieces are **not yet implemented** and are tracked in the
server loop epic (`rpg-2ob4`):

1. **Main server fiber** — the orchestration fiber that runs stages 1–6
   in sequence each tick.  Currently there is no single entry point that
   ties drain → physics → replication together outside the removed demo.

2. **Player controller stage** — kinematic velocity integration from
   `INPUT_MOVE`/`INPUT_ROT` into `phys_game_state_t.players[]`.  The
   schemas and physics game_state struct exist; the glue does not.

3. **Reliable event system** — `server_event_type_t` enum,
   `server_event_t` union, event queue, and encode/decode functions.
   Header: `include/ferrum/server/event.h`.  Replaces the spawn-only
   reliable path with a general-purpose event broadcast that also
   covers despawn, death, health, status effects, and inventory.

4. **Tiered send frequency** — `repl_server_tick` currently sends every
   body every tick.  Tier-based decimation (T2 every 2nd, T3 every 4th,
   T4 every 8th) needs to be added.

5. **Tiered quantization selection** — T0/T1 should use full-float
   `STATE_CUBE` encoding; T2–T4 can use the existing quantized
   `BODY_STATE` format.

6. **Tick pacing / catch-up policy** — max catch-up ticks, fixed timestep
   accumulator, and backpressure (skip replication if physics is behind).

7. **Integration tests** — a headless test that proves the loop advances
   ticks, dispatches physics, and produces replication output without
   SDL or graphics.
