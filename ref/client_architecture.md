# Client Architecture

## Overview

The client is an SDL2 + OpenGL application.
In early phases it is primarily a **physics test client**:

- connect to server
- receive **reliable** world events (spawn/despawn/death/mesh data)
- receive high-rate **unreliable** rigid body movement snapshots
- interpolate and render debug-first visuals (cubes + wireframe colliders + grid)

Later phases expand into an interactive FPS-style controller and richer rendering
(skinned meshes, static geo, debug primitives, etc.).

Design goals:
- **Encapsulation:** reliable delivery is isolated behind a stream abstraction (RUDP now, TCP later)
- **Simplicity:** most traffic is raw datagrams (no stream semantics)
- **Debuggability:** explicit layouts + explicit failure modes

Related docs:
- `ref/server_architecture.md`
- `ref/networking_runtime.md`
- `ref/client_frame_callgraph.md`

---

## Threading Model

```
+----------------------------+
| Main thread (SDL + GL)     |  input, ECS, interpolation, rendering
+-------------+--------------+
              |
+-------------v--------------+
| RX thread (fr_client_rx_t) |  recvfrom, RUDP demux, stream reassembly, topic push
+----------------------------+
              |
+-------------v--------------+
| TX thread (fr_client_tx_t) |  drain outbound queue, serialize frames, sendto
+----------------------------+
              |
+-------------v--------------+
| Prediction thread          |  fixed-timestep integration, server reconciliation
| (fr_prediction_tick_t)     |  reads bodies_net, writes bodies_next, swaps with curr
+----------------------------+
              |
+-------------v--------------+
| Encode thread (optional)   |  video capture: reads CPU frame ring -> ffmpeg pipe
+----------------------------+
```

The OpenGL context stays on the main thread.
The RX and TX threads are the only threads allowed to touch sockets.
The prediction thread runs client-side physics integration at the server tick rate.
The encode thread (spawned by `fr_video_capture_create`) never touches GL;
it consumes pixel data from a lock-free SPSC ring and pipes it to ffmpeg.
When built with `FR_NET_EMULATION` (`make EMU=1`), outbound `sendto` calls
route through the in-process net_emulator delay queue (configured via engine
settings before launch). See `ref/networking_callgraph.md`.

---

## Transport Model

### Reliable events: stream abstraction

Reliable events are ordered and retransmitted (when over UDP) via a **stream**.
The purpose of the stream is reconstruction + ordering and to allow swapping the
underlying transport later.

- current transport: RUDP + `NET_REPL_SCHEMA_STREAM_FRAME` -> `fr_rudp_stream_push_frame()`
- future transport: TCP (same message framing, different I/O)

**Stream message layout (delivered to main thread):**

```
[schema_id:u16 LE] [payload...]
```

### Unreliable state: RUDP datagrams

Rigid body state updates are sent as **RUDP unreliable packets** (schema-prefixed
within RUDP framing for ACK piggyback).  No ordering guarantee; newest wins.

**Datagram layout (delivered to main thread):**

```
[schema_id:u16 LE] [payload...]
```

**Demux rule on receive (RX thread):**
- if first 4 bytes match RUDP protocol ID (`NET_RUDP_PROTOCOL_ID_P008`, 0x52555038) -> feed RUDP peer -> extract schema_id + payload
- else -> treat as raw UDP datagram and parse `schema_id:u16 LE` directly

---

## Client RX Runtime (`fr_client_rx_t`)

- Dedicated receive thread managed by `fr_client_rx_start()` / `fr_client_rx_stop()`.
- Reads UDP packets via socket or test callback (`recv_cb`).
- Feeds packets through RUDP peer for ACK handling + reliability processing.
- Reconstructs reliable stream messages via `fr_rudp_stream_push_frame()`.
- Pushes decoded messages into topic channels (configurable array in config).
- Supports test-only frame injection: `fr_client_rx_inject()`.
- Configurable: max channels (default 16), max pending per channel (default 64).
- Bind internal socket: `fr_client_rx_bind_ipv4()`.

---

## Client TX Runtime (`fr_client_tx_t`)

- Dedicated transmit pump thread managed by `fr_client_tx_start()` / `fr_client_tx_stop()`.
- Thread-safe enqueue: `fr_client_tx_enqueue(channel_id, payload, len)`.
- Drains outbound stream via sendto callback.
- Rate limiting: `max_packets_per_pump` caps packets per pump cycle.
- Manual pump for testing: `fr_client_tx_pump_once()`.
- Configurable: max channels (default 4), max pending per channel (default 64),
  max payload size (default 1024).

---

## Reliable Event Model

All reliable "things that must happen" are expressed as typed events.

### Event types (server -> client)

- `NET_REPL_EVENT_SPAWN` (1) -- entity appears in world
- `NET_REPL_EVENT_DESPAWN` (2) -- entity removed from world
- `NET_REPL_EVENT_DEATH` (3) -- entity destroyed

Event batch wire format (reliable stream message):
```
[count:u16] [server_tick:u16]
per entry:
  [event_type:u8] [reserved:u8] [entity_key:u64] [payload_size:u16] [payload...]
```

### Message types

| Message | Schema | Direction | Payload size |
|---------|--------|-----------|--------------|
| JOIN | 0x2001 | client->server | 4 bytes (client_nonce:u32) |
| WELCOME | 0x2004 | server->client | 4 bytes (expected_entities:u16, tick_hz:u16) |
| BODY_SPAWN | 0x200A | server->client | 33 bytes (body_id, shape, pos, rot, half_extents) |
| SPAWN_BATCH | 0x2005 | server->client | variable (count + entries) |
| EVENT | 0x200D | server->client | variable (batched events) |
| MESH_DATA | 0x2010 | server->client | chunked (10-byte header + up to 440 bytes payload) |
| INPUT_ROT | 0x2007 | client->server | 16 bytes (entity_id, event_id, axis, speed) |
| INPUT_MOVE | 0x2008 | client->server | (defined in common.h) |
| INPUT_SPAWN | 0x2009 | client->server | (defined in common.h) |

Important ordering contract:
- BODY_SPAWN / DESPAWN are reliable and ordered
- state updates for a body may arrive before its BODY_SPAWN; client must drop/buffer

---

## Identity Linking: ECS entities and network bodies

- BODY_SPAWN events include a `body_id`.
- The spawned ECS entity stores `body_id` in a component.

This keeps identity explicit and makes state application debuggable.

### Ghost Table

`net_ghost_table_t` maps server entity IDs (u32) to client-local entity handles
(`net_ghost_entity_t`: index + generation for stale detection).  Fixed-capacity,
caller-owned storage, linear scan.  Operations: create, lookup, destroy, clear.

---

## Body State Inbox

`fr_body_state_inbox_t` provides newest-wins semantics for unreliable BODY_STATE:
- Per `body_id`, only updates with a strictly newer `server_tick` are accepted.
- Out-of-order / duplicate datagrams are silently ignored.
- Stores the latest `net_repl_body_state_t` per body for sampling.

---

## Pose Interpolation

The pose interpolator (`fr_pose_interpolator_t`) smooths replicated body movement
between server ticks.  Each entity maintains its own interpolator instance.

### Push / Sample API

```
push(recv_time, pos, rot, vel, ang_vel, server_time_s)
sample(render_time, quat_epsilon, &out_pos, &out_rot)
```

- `push()` promotes the current snapshot to `prev` and stores the new one as `curr`.
  Server-authoritative linear and angular velocity are stored alongside the position
  and orientation at each endpoint.
- `sample()` computes a normalized `t` in the `[prev, curr]` time window.

### Interpolation (t <= 1)

Position and rotation are interpolated as a coupled rigid body attitude:

1. **Forward estimate:** integrate `prev` forward by `t * dt` using `prev_vel` / `prev_ang_vel`.
2. **Backward estimate:** integrate `curr` backward by `(1-t) * dt` using `curr_vel` / `curr_ang_vel`.
3. **Blend:** lerp positions and slerp rotations with weight `t`.

This keeps translation and rotation physically consistent -- a rotating body's CoM
traces the correct arc rather than cutting a straight line between snapshots.

With zero velocities (or a stationary body) the estimates collapse to `prev`/`curr`
and the blend degenerates to plain lerp/slerp.

### Extrapolation (t > 1)

Extrapolation is **disabled**.  When `t > 1` the interpolator holds `curr` pose.
At any meaningful velocity the per-frame displacement exceeds constraint tolerances,
producing visible joint popping.

### Whole-World Snapshot Interpolation (`fr_snapshot_interp_t`)

Higher-level module maintaining an array of per-body pose interpolators:
- Fed from decoded server physics snapshots (`phys_snapshot`).
- Dequantizes each body and pushes pos/rot/vel into the corresponding interpolator.
- Stale snapshots (tick <= last_tick) are silently dropped.
- `fr_snapshot_interp_sample()` returns interpolated pose for any body index.

### Correction Debug Lines

Yellow lines visualize the correction jump when a new server snapshot arrives:

1. Sample the interpolator at `render_time` **before** the push -> `old_pos`.
2. Push the new snapshot.
3. Sample the interpolator at the **same** `render_time` after the push -> `new_pos`.
4. Draw yellow lines from `old_pos` corners to `new_pos` corners, fading over 0.5 s.

This shows the actual instantaneous correction, not the natural travel distance.

---

## Client-Side Prediction

### Input Prediction (`net_predict_ctx_t`)

- Stores inputs in a ring buffer keyed by tick.
- Each tick: apply local input to advance predicted state (optimistic).
- On server state arrival: rewind to server state, replay unconfirmed inputs,
  compare result to old prediction.
  - Error > `snap_threshold` -> hard snap to replayed state.
  - Error > `blend_threshold` -> soft blend toward replayed state.
  - Otherwise -> no correction needed.
- Simulation step provided as a callback (`net_predict_sim_fn`), decoupling from physics.

### Prediction Tick Thread (`fr_prediction_tick_t`)

- Dedicated pthread running at fixed timestep matching server physics rate.
- Triple-buffer architecture:
  - `bodies_curr`: render thread reads (lock-free).
  - `bodies_next`: prediction thread writes.
  - `bodies_net`: recv thread writes server-authoritative state via atomic dirty flags.
- Each tick:
  1. Copy curr -> next.
  2. If bodies_net has new server data (atomic dirty flag), reconcile next toward
     server state using `phys_prediction_config_t` (snap/blend thresholds).
  3. Integrate next forward: apply gravity, advance position by linear velocity,
     advance orientation by angular velocity.
  4. Swap curr <-> next.
- Constants: velocity damping 0.999/s, max linear speed 100 m/s, max angular speed 50 rad/s.

---

## Mesh Data Reception

Large mesh geometry arrives as `MESH_DATA` (0x2010) reliable chunked messages:

Per-chunk wire layout:
```
[body_id:u16] [chunk_index:u16] [total_chunks:u16] [total_size:u32] [payload:N bytes]
```

`net_repl_mesh_reassembly_table_t` tracks per-body reassembly:
- Fixed-capacity table of reassembly slots.
- 32-bit received mask (max 32 chunks per mesh).
- On completion, caller takes ownership of the reassembled buffer (must free).
- Max supported mesh size: 256 KiB.

---

## Time Synchronization

`net_time_sync_t`: offset estimator using sliding-window median filter (up to 32 samples).
- Server embeds timestamp in each packet.
- Client feeds `(server_time_ms, client_time_ms)` pairs.
- Offset = server_time - client_time, filtered to reject outliers.
- Drift clamp limits offset change rate per update.
- Result: `estimated_server_now = client_now + offset`.

`net_jitter_buffer_t`: tracks arrival jitter, produces a margin for interpolation delay.
- Max-observed jitter in a 32-sample window.
- Used to buffer interpolation timing so snapshots arrive before needed.

---

## Video Capture

The optional video capture module (`fr_video_capture_t`) records the rendered
framebuffer to an MP4 file without stalling the render loop.

### Architecture

```
Render thread (GL)          CPU ring (SPSC)        Encode thread
  glReadPixels -> PBO -->  frame_ring_push() -->  fwrite -> ffmpeg pipe
  (4-slot PBO ring)         (8-slot ring)          (libx264, CRF 20)
```

- **PBO ring** (4 slots): async GPU->CPU readback via `GL_PIXEL_PACK_BUFFER`
  with fence sync.  Non-blocking -- never stalls the render loop.
- **Frame ring** (8 slots, SPSC lock-free): transfers completed pixel buffers
  from render thread to encode thread.  Backpressure drops oldest frame.
- **Encode thread**: pipes raw RGBA to ffmpeg for H.264 encoding.  Falls back
  to raw `.raw` file if ffmpeg is not on PATH.

### Frame Decimation

The capture module decimates to the target FPS (`desc.fps`, default 30).
`submit_frame()` checks `CLOCK_MONOTONIC` and only initiates a PBO readback
when at least `1/fps` seconds have elapsed since the last capture.  This
produces exactly one unique frame per output video frame -- no duplication,
no temporal aliasing.

### Usage

```c
fr_video_capture_t *cap = fr_video_capture_create(&(fr_video_capture_desc_t){
    .width = W, .height = H, .fps = 30, .output_path = "out.mp4",
});
// per frame, after draw, before swap:
fr_video_capture_submit_frame(cap);
// on shutdown:
fr_video_capture_destroy(cap);  // flushes + joins encode thread
```

Demo client: `./build/demo_client IP PORT SECS --record capture.mp4`

---

## Rendering Scope

### Phase 1 rendering (physics test client)

- hardcoded cube mesh instances for bodies
- wireframe collider overlay (box/sphere/capsule)
- ground grid, trace lines

This validates:
- reliable stream ordering
- unreliable snapshot application
- interpolation and visual stability

### Longer-term rendering

- static geometry (level mesh, props)
- skinned meshes (characters) using existing skinning pipeline
- debug primitives system (lines, wireframes, contact points, broadphase grid)

---

## Debug Primitives (Feature)

Physics iteration requires robust debug drawing.
Keep this system isolated and easy to toggle.

Planned primitive set:
- lines (traces, normals)
- wireframe boxes/spheres/capsules (colliders)
- grid
- contact points

All debug rendering should be gated behind a compile-time flag (and ideally runtime toggles)
so it can be stripped or disabled cleanly.

---

## Join / Spawn Flow (Client Perspective)

1. Client sends `JOIN` (0x2001, reliable) with a `client_nonce:u32`.
2. Server responds with `WELCOME` (0x2004, reliable): `expected_entities:u16`, `tick_hz:u16`.
3. Server sends `BODY_SPAWN` (0x200A) messages for all existing entities:
   body_id, shape_type, color_seed, initial position/orientation, half-extents.
4. For mesh-type bodies, server sends `MESH_DATA` (0x2010) chunks reliably.
5. Client creates entities, registers ghost table mappings, starts accepting
   BODY_STATE (0x200B) unreliable updates.
6. Client pushes body states into `fr_body_state_inbox_t` (newest-wins) and
   feeds pose interpolators for smooth rendering.

---

## Phased Implementation

### Phase 1 -- Physics test client (first priority)

Goal: spawn + movement correctness.
- RX/TX threads + demux (RUDP stream + raw UDP)
- decode BODY_SPAWN events and create entities with `body_id`
- decode BODY_STATE datagrams and push into pose interpolators
- render cubes + grid + collider wireframes

### Phase 2 -- Interactive controller

- FPS mouse look + WASD
- send `INPUT_MOVE` / `INPUT_ROT` (enqueued via `fr_client_tx_enqueue`)
- client-side prediction via `fr_prediction_tick_t`

### Phase 3 -- Full event coverage

- implement handlers for all event types (death/health/status/inventory)
- mesh data reassembly for mesh-type bodies

### Phase 4 -- Debug primitives expansion

- contact points, broadphase grid cells, constraint visualization

### Phase 5 -- Assets + skinned meshes

- load meshes, render animated characters

---

## Failure Modes / Pitfalls

- **State before spawn:** drop or buffer briefly per `body_id` using `fr_body_state_inbox_t`
  newest-wins semantics (stale ticks are ignored regardless).
- **Protocol demux:** keep "RUDP vs raw UDP" detection explicit (4-byte protocol ID check).
- **Backpressure:** spawn bursts must be batched; reliable stream must not silently drop
  (stream channel full pushes message back to topic for retry).
- **Thread ownership:** never call socket APIs from the main thread. Only RX/TX threads
  touch sockets.
- **Prediction drift:** prediction tick uses reconciliation thresholds; large errors
  snap, small errors blend, negligible errors are ignored.
- **Ghost table stale entries:** generation counter in `net_ghost_entity_t` detects
  stale entity references after despawn/respawn.
- **Mesh reassembly timeout:** no built-in timeout; if chunks never arrive, the slot
  remains occupied. Capacity-limited reassembly table bounds memory.
