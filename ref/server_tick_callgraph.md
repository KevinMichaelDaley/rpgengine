# Server Tick Call Graph & Data Flow

This document describes the server tick orchestration and data flow
between subsystems.  See `ref/server_architecture.md` for the full
design narrative.

Source of truth:
- `src/physics/world/phys_tick_runner.c` — physics fiber loop
- `src/server/repl/repl_server_tick.c` — replication encode + dispatch
- `src/server/net/runtime/runtime_client_fiber.c` — per-client net pump
- `src/server/net/runtime/runtime_pump.c` — top-level event pump

Public entry points (planned; not all exist yet):
```c
void server_tick(server_t *srv);                    /* main orchestrator (to build) */
void phys_tick_runner_start(phys_tick_runner_t *r);  /* physics fiber               */
void fr_server_client_fiber_main(void *user_data);   /* per-client net fiber        */
```

---

## High-Level Call Graph

```text
server_tick(srv)
  │
  ├─ Stage 1: drain_inbound_messages(srv)
  │    ├─ for each client:
  │    │    └─ fr_topic_channel_pop(srv->inbound_topic, buf, &len)
  │    ├─ decode schema_id from buf[0..1]
  │    └─ switch (schema_id):
  │         ├─ INPUT_MOVE  → input_queue_push(&srv->input_queue, client_id, &move)
  │         ├─ INPUT_ROT   → input_queue_push(&srv->input_queue, client_id, &rot)
  │         └─ INPUT_SPAWN → fr_topic_channel_push(srv->phys_cmd_channel, &spawn_cmd)
  │
  ├─ Stage 2: apply_player_controllers(srv)
  │    └─ for each connected player:
  │         ├─ input = input_queue_latest(&srv->input_queue, client_id)
  │         ├─ forward = basis_from_yaw(input.yaw)
  │         ├─ vel = forward * input.move_z + right * input.move_x
  │         └─ srv->game_state.players[i].velocity = vel
  │              srv->game_state.players[i].look_dir = input.look_dir
  │
  ├─ Stage 3: (physics tick runs concurrently on physics fiber)
  │    │  phys_tick_runner fiber loop:
  │    │    ├─ pace at fixed dt (job_yield until elapsed)
  │    │    ├─ phys_cmd_drain(world, cmd_channel, spawn_cb, user)
  │    │    ├─ phys_world_tick_parallel(world, &srv->game_state, jobs)
  │    │    │    └─ [see ref/physics_tick_callgraph.md for 14-stage pipeline]
  │    │    ├─ phys_cmd_drain(world, correction_channel, NULL, NULL)
  │    │    └─ atomic_fetch_add(&runner.completed_ticks, 1)
  │    │
  │    └─ (main fiber does NOT block here; physics runs asynchronously)
  │
  ├─ Stage 4: wait_physics_barrier(srv)
  │    └─ while (phys_tick_runner_tick_id(&srv->runner) < srv->expected_tick):
  │         job_yield()
  │
  ├─ Stage 5: broadcast_reliable_events(srv)
  │    ├─ server_event_queue_drain(&srv->event_queue)
  │    └─ for each event:
  │         ├─ server_event_encode(&event, buf, &len)
  │         └─ for each client:
  │              └─ fr_topic_channel_push(client->out_reliable,
  │                   [NET_REPL_SCHEMA_EVENT | buf])
  │
  │    Events generated during stages 1–4:
  │    ├─ SPAWN    ← spawn_cb in phys_cmd_drain / ecs_world_create_entity
  │    ├─ DESPAWN  ← entity removal / body deactivation
  │    ├─ DEATH    ← gameplay logic (HP ≤ 0)
  │    ├─ HEALTH   ← damage / heal events
  │    ├─ STATUS   ← status effect apply / expire
  │    └─ INVENTORY← item pickup / use / drop
  │
  ├─ Stage 6: broadcast_body_state(srv)
  │    ├─ for each active rigid body:
  │    │    ├─ tier = world->tiers[body_id]
  │    │    ├─ if (srv->tick_count % tier_decimation[tier] != 0) continue
  │    │    ├─ net_repl_body_state_encode(body, &msg)
  │    │    │    (T0/T1: full float STATE_CUBE; T2–T4: quantized BODY_STATE)
  │    │    └─ for each client:
  │    │         └─ fr_topic_channel_push(client->out_unreliable,
  │    │              [schema_id | msg])
  │    └─ tier_decimation = { T0:1, T1:1, T2:2, T3:4, T4:8 }
  │
  ├─ Stage 7: (client fibers flush autonomously)
  │    │  fr_server_client_fiber_main(client_data):
  │    │    loop:
  │    │      ├─ pump_reliable_to_stream_(stream, client->out_reliable)
  │    │      │    ├─ fr_topic_channel_pop(out_reliable, buf, &len)
  │    │      │    ├─ fr_rudp_stream_send(stream, channel=0, buf, len)
  │    │      │    └─ fr_rudp_stream_flush_send(stream, stream_sendto_, user)
  │    │      │         └─ net_rudp_peer_send_reliable_via(peer, STREAM_FRAME, frame)
  │    │      ├─ pump_unreliable_topic_(rt, client_id, out_unreliable, &peer, now)
  │    │      │    ├─ fr_topic_channel_pop(out_unreliable, buf, &len)
  │    │      │    └─ net_rudp_peer_send_unreliable_via(peer, schema_id, payload)
  │    │      ├─ net_rudp_peer_tick_resend_via(peer, ...)  /* retransmit lost reliable */
  │    │      ├─ publish_inbound_(rt, client_id, ...)      /* inbound → inbound_topic  */
  │    │      └─ job_yield()
  │    │
  │    └─ (main fiber does NOT wait for flush; client fibers are autonomous)
  │
  └─ srv->tick_count++
```

---

## Data Flow Diagram

```text
┌──────────────────────────────────────────────────────────────────────┐
│                         SERVER PROCESS                               │
│                                                                      │
│  ┌────────────┐    inbound_topic     ┌─────────────────────┐         │
│  │ Client     │──────────────────────│ Stage 1: Drain      │         │
│  │ Fiber (RX) │   (INPUT_MOVE,       │ Inbound Messages    │         │
│  │            │    INPUT_ROT,        └──────┬──────────────┘         │
│  │  per client│    INPUT_SPAWN)             │                        │
│  └────────────┘                             │ input_queue            │
│        ▲  ▲                                 ▼                        │
│        │  │                          ┌─────────────────────┐         │
│        │  │                          │ Stage 2: Apply      │         │
│        │  │                          │ Player Controllers  │         │
│        │  │                          └──────┬──────────────┘         │
│        │  │                                 │ game_state.players[]   │
│        │  │                                 ▼                        │
│        │  │   phys_cmd_channel       ┌─────────────────────┐         │
│        │  │  ─────────────────────── │ Stage 3: Physics    │         │
│        │  │                          │ Tick (async fiber)  │         │
│        │  │   correction_channel     │                     │         │
│        │  │  ─────────────────────── │ phys_world_tick_    │         │
│        │  │                          │   parallel()        │         │
│        │  │                          └──────┬──────────────┘         │
│        │  │                                 │ completed_ticks++      │
│        │  │                                 ▼                        │
│        │  │                          ┌─────────────────────┐         │
│        │  │                          │ Stage 4: Barrier    │         │
│        │  │                          │ (yield until done)  │         │
│        │  │                          └──────┬──────────────┘         │
│        │  │                                 │                        │
│        │  │                      ┌──────────┴──────────┐             │
│        │  │                      ▼                     ▼             │
│        │  │    ┌─────────────────────┐  ┌─────────────────────┐      │
│        │  │    │ Stage 5: Reliable   │  │ Stage 6: Unreliable │      │
│        │  │    │ Events (spawn,      │  │ Body State          │      │
│        │  │    │ despawn, death, ...) │  │ (tiered frequency)  │      │
│        │  │    └──────┬──────────────┘  └──────┬──────────────┘      │
│        │  │           │ out_reliable            │ out_unreliable      │
│        │  │           ▼                         ▼                     │
│        │  │    ┌─────────────────────────────────────┐               │
│        │  │    │ Stage 7: Client Fiber (TX)          │               │
│        │  └────│  pump reliable → stream → RUDP      │               │
│        │       │  pump unreliable → direct UDP       │               │
│        └───────│  retransmit lost reliable frames    │               │
│     ACKs recv  └─────────────────────────────────────┘               │
│                                                                      │
└──────────────────────────────────────────────────────────────────────┘
```

### Data stores read/written per stage

| Stage | Reads                                    | Writes                                  |
|-------|------------------------------------------|-----------------------------------------|
| 1     | `inbound_topic`                          | `input_queue`, `phys_cmd_channel`       |
| 2     | `input_queue`                            | `game_state.players[]`                  |
| 3     | `phys_cmd_channel`, `correction_channel`, `game_state` | `phys_world_t` bodies, `completed_ticks` |
| 4     | `completed_ticks`                        | —                                       |
| 5     | `event_queue`                            | `client.out_reliable` (per client)      |
| 6     | `phys_world_t` bodies, tier data         | `client.out_unreliable` (per client)    |
| 7     | `client.out_reliable`, `client.out_unreliable` | RUDP socket (wire)                |

### Data ownership boundaries

```text
  game_state.players[]    ──── owned by main fiber, read by physics fiber
  phys_world_t            ──── owned by physics fiber (main reads after barrier)
  event_queue             ──── written by stages 1-4, drained by stage 5
  input_queue             ──── written by stage 1, read by stage 2
  client.out_reliable     ──── written by stage 5, read by client fiber
  client.out_unreliable   ──── written by stage 6, read by client fiber
  inbound_topic           ──── written by client fibers (RX), read by stage 1
  phys_cmd_channel        ──── written by stage 1, read by physics fiber
  correction_channel      ──── written by main fiber, read by physics fiber
```

All cross-fiber communication uses `fr_topic_channel_t` (lock-free ring
buffer with configurable backpressure).  No shared mutable state is
accessed without a topic channel or atomic barrier.

---

## Tick Timing

```text
  t=0          t=dt         t=2dt        t=3dt
  │            │            │            │
  ├── tick 0 ──├── tick 1 ──├── tick 2 ──├──
  │            │            │            │
  │ drain      │ drain      │ drain      │
  │ controllers│ controllers│ controllers│
  │ barrier    │ barrier    │ barrier    │
  │ repl       │ repl       │ repl       │
  │            │            │            │

  Physics fiber runs continuously, pacing itself at fixed dt.
  Main fiber synchronizes via barrier (stage 4) each tick.
  Client fibers flush continuously (not tick-aligned).
```

Catch-up policy: if the main fiber falls behind, it may run up to
`MAX_CATCHUP_TICKS` (e.g., 3) consecutive ticks without sleeping.
Beyond that, it drops ticks and logs a warning.
