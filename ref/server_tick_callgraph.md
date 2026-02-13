# Server Tick Call Graph & Data Flow (Target)

This document describes the target server tick orchestration and data flow.
See `ref/server_architecture.md` for the narrative design.

### EXISTS vs PLANNED

This is the **target** tick layout. The repository already contains the physics runner,
RUDP peer, stream framing, and replication encoders, but the I/O-thread + looping encoder job
wiring is the intended end state.

Source of truth (existing building blocks):
- `include/ferrum/server/net/inbound_message.h` — inbound topic layout
- `src/physics/world/phys_tick_runner.c` — physics runner fiber
- `src/server/repl/repl_server_tick.c` — replication encode utilities
- `src/net/rudp/stream/stream_io.c` — stream framing + flush

---

## High-Level Call Graph

```text
server_main()
  ├─ start_io_thread(io)
  ├─ start_physics_runner(runner)
  ├─ start_encoder_jobs(encoders)          (looping jobs)
  └─ while (running):
       server_tick(srv)

server_tick(srv)
  ├─ Stage 1: drain_inbound_messages(srv)
  │    └─ while (fr_topic_channel_pop(srv->inbound_topic, msg, &len)):
  │         ├─ fr_server_net_inbound_message_decode(&view, msg, len)
  │         └─ switch (view.schema_id):
  │              ├─ INPUT_MOVE  → input_queue_set_latest(client_id, move)
  │              ├─ INPUT_ROT   → input_queue_set_latest(client_id, rot)
  │              └─ INPUT_SPAWN → fr_topic_channel_push(srv->phys_cmd_channel, spawn_cmd)
  │
  ├─ Stage 2: apply_controllers_and_gameplay(srv)
  │    ├─ for each player:
  │    │    └─ compute kinematic intent → write into ECS components
  │    └─ gameplay systems:
  │         ├─ damage/heal → queue SERVER_EVENT_HEALTH
  │         ├─ death       → queue SERVER_EVENT_DEATH
  │         ├─ despawn     → queue SERVER_EVENT_DESPAWN
  │         └─ spawns      → queue SERVER_EVENT_SPAWN
  │
  ├─ Stage 3: pre_physics_sync_par(srv)
  │    ├─ job_dispatch N batches over bodies/entities
  │    └─ for each kinematic body:
  │         ├─ body_next.linear_vel = sum(controller intents)
  │         ├─ body_next.kinematic_target = ecs_transform
  │         └─ body_next.entity_index = entity.index
  │
  ├─ Stage 4: kick_physics_tick(srv)
  │    └─ physics runner thread loop (dedicated pthread, independent pacing):
  │         ├─ pace: nanosleep until fixed_dt elapsed since last tick
  │         ├─ overload detection (64-tick rolling history, 10% tolerance):
  │         │    ├─ enter variable-dt: 48/64 ticks overran (75%, ~1s sustained)
  │         │    ├─ exit variable-dt: 6/8 recent ticks on-time (~130ms recovery)
  │         │    └─ sets world->dt_override = wall_elapsed (or 0 if normal)
  │         ├─ phys_cmd_drain(world, cmd_channel, spawn_cb, user)
  │         ├─ phys_world_tick_parallel(world, &srv->game_state, jobs)
  │         └─ completed_ticks++
  │
  ├─ Stage 5: wait_physics_barrier(srv)
  │    └─ while (runner.completed_ticks < expected_tick): job_yield()
  │
  ├─ Stage 6: post_physics_observe(srv)
  │    └─ read-only observe of phys_world_t (for UI/stats/gameplay readbacks)
  │
  ├─ Stage 7: enqueue_reliable_event_batch(srv)
  │    └─ push tick’s server_event_queue into encoder-visible ring
  │
  ├─ Stage 8: publish_state_snapshot(srv)
  │    └─ publish pointer/handle to the authoritative body state buffers
  │
  └─ tick_count++

Encoder job: reliable_event_encoder_loop(client_id)
  ├─ wait for next tick’s event batch
  ├─ batch encode:
  │    └─ [NET_REPL_SCHEMA_EVENT:u16][event_type:u8][entity_key:u64][payload...]
  └─ enqueue to io->reliable_stream_tx_queue

Encoder job: body_state_encoder_loop(client_id)
  ├─ wait for next tick’s published snapshot handle
  ├─ for bodies (tiered decimation optional):
  │    ├─ encode BODY_STATE or STATE_CUBE into datagram:
  │    │    └─ [schema_id:u16][payload...]
  │    └─ enqueue to io->udp_tx_queue

IO thread: io_thread_main()
  ├─ RX:
  │    ├─ recvfrom(udp)
  │    ├─ if packet is RUDP:
  │    │    ├─ net_rudp_peer_receive(peer, ...)
  │    │    └─ if schema == STREAM_FRAME: fr_rudp_stream_push_frame(stream, payload)
  │    └─ else:
  │         └─ decode schema_id and push raw msg to inbound_topic
  ├─ TX reliable:
  │    ├─ drain reliable_stream_tx_queue → fr_rudp_stream_send()
  │    ├─ fr_rudp_stream_flush_send(stream, sendto_cb)
  │    └─ net_rudp_peer_tick_resend_via(peer, ...)
  └─ TX unreliable:
       └─ drain udp_tx_queue → net_udp_socket_sendto()
```

---

## Data Flow Diagram (DFD)

```text
┌───────────────────────────────────────────────────────────────────────────┐
│                              SERVER PROCESS                               │
│                                                                           │
│  UDP socket(s) owned by I/O thread                                         │
│                                                                           │
│  ┌──────────────────────────────┐             ┌─────────────────────────┐ │
│  │ I/O thread (RX/TX demux)     │             │ Main tick fiber         │ │
│  │                              │             │                         │ │
│  │  RX: recvfrom(udp)           │             │ 1) drain inbound_topic  │ │
│  │   ├─ RUDP? → stream push     │──inbound───►│ 2) controllers/gameplay │ │
│  │   └─ raw?  → inbound_topic   │   topic     │ 3) pre-phys sync (par)  │ │
│  │                              │             │ 4) wait barrier         │ │
│  │  TX: sendto(udp)             │◄──tx queues─│ 7/8) publish work to enc │ │
│  └──────────────┬───────────────┘             └──────────────┬──────────┘ │
│                 │                                            │            │
│                 │                                            │            │
│        ┌────────▼────────┐                         ┌─────────▼─────────┐ │
│        │ Reliable stream │                         │ Physics runner     │ │
│        │ (RUDP+stream)   │                         │ fiber              │ │
│        │ fr_rudp_stream  │                         │ phys_world_tick_*  │ │
│        └────────┬────────┘                         └─────────┬─────────┘ │
│                 │                                            │            │
│        ┌────────▼─────────┐                        completed_ticks        │
│        │ Encoder jobs      │                                              │
│        │ (looping)         │                                              │
│        │ - event encoder   │                                              │
│        │ - state encoder   │                                              │
│        └────────┬─────────┘                                              │
│                 │                                                        │
│        tx queues to I/O thread (already-encoded bytes)                    │
│                                                                           │
└───────────────────────────────────────────────────────────────────────────┘
```

---

## Message / Queue Layouts

### inbound_topic (I/O → main tick)

Defined by `fr_server_net_inbound_message_encode/decode`:

```
[client_id:u16][schema_id:u16][flags:u8][reserved:u8][payload...]
```

### reliable stream messages (encoder → I/O)

```
[schema_id:u16][payload...]
```

### unreliable state datagrams (encoder → I/O)

```
[schema_id:u16][payload...]
```

---

## Failure Modes / Pitfalls

- Encoder jobs must not read `phys_world_t` while physics is mutating it; use the barrier and/or snapshot handles.
- Queues must be sized for spawn bursts (batch spawns; stream slot windows).
- Ensure despawn is reliable and ordered relative to subsequent spawns of the same body_id.
