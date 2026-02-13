# Client Architecture (Target)

## Overview

The client is an SDL2 + OpenGL application.
In early phases it is primarily a **physics test client**:

- connect to server
- receive **reliable** world events (spawn/despawn/death/…)
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
- `ref/client_frame_callgraph.md`

### EXISTS vs PLANNED

These docs describe the **target** layout.
The repository already contains key building blocks (stream reassembly, renderer, pose interpolator),
but the “network I/O thread that demuxes RUDP vs raw UDP” is an architectural requirement.

---

## Threading Model

```
┌──────────────────────────┐
│ Main thread (SDL + GL)   │  input, ECS, interpolation, rendering
└─────────────┬────────────┘
              │
┌─────────────▼────────────┐
│ I/O thread (sockets)     │  recvfrom/sendto, demux, reassembly, queues
└──────────────────────────┘
              │
┌─────────────▼────────────┐
│ Encode thread (optional) │  video capture: reads CPU frame ring → ffmpeg pipe
└──────────────────────────┘
```

The OpenGL context stays on the main thread.
The I/O thread is the only thread allowed to touch sockets.
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

- current transport: RUDP + `NET_REPL_SCHEMA_STREAM_FRAME` → `fr_rudp_stream_push_frame()`
- future transport: TCP (same message framing, different I/O)

**Stream message layout (delivered to main thread):**

```
[schema_id:u16 LE] [payload...]
```

### Unreliable state: raw UDP datagrams

Rigid body state updates are **plain UDP datagrams**, MTU-sized.
No ordering guarantee; newest wins.

**Datagram layout (delivered to main thread):**

```
[schema_id:u16 LE] [payload...]
```

**Demux rule on receive:**
- if packet begins with RUDP protocol header/magic → feed RUDP peer → stream
- else → treat as raw UDP datagram and parse `schema_id` directly

---

## Reliable Event Model

All reliable “things that must happen” are expressed as typed events.
(Exact enum/union lives in the server doc; the client must be able to handle them all.)

Minimum event set:
- SPAWN, DESPAWN
- DEATH
- HEALTH
- STATUS
- INVENTORY

Important ordering contract:
- SPAWN/DESPAWN are reliable and ordered
- state updates for a body may arrive before its SPAWN; client must drop/buffer

---

## Identity Linking: ECS entities and network bodies

- Spawn events include a `body_id`.
- The spawned ECS entity stores `body_id` in a component.

This keeps identity explicit and makes state application debuggable.
A derived cache `entity_by_body_id[]` is allowed for O(1) lookup.

---

## Pose Interpolation

The pose interpolator (`fr_pose_interpolator_t`) smooths replicated body movement
between server ticks.  Each entity maintains its own interpolator instance.

### Push / Sample API

```
push(recv_time, pos, rot, vel, ang_vel, server_time_s)
sample(render_time, &out_pos, &out_rot)
```

- `push()` promotes the current snapshot to `prev` and stores the new one as `curr`.
  Server-authoritative linear and angular velocity are stored alongside the position
  and orientation at each endpoint.
- `sample()` computes a normalized `t` in the `[prev, curr]` time window.

### Interpolation (t ≤ 1)

Position and rotation are interpolated as a coupled rigid body attitude:

1. **Forward estimate:** integrate `prev` forward by `t × dt` using `prev_vel` / `prev_ang_vel`.
2. **Backward estimate:** integrate `curr` backward by `(1−t) × dt` using `curr_vel` / `curr_ang_vel`.
3. **Blend:** lerp positions and slerp rotations with weight `t`.

This keeps translation and rotation physically consistent — a rotating body's CoM
traces the correct arc rather than cutting a straight line between snapshots.

With zero velocities (or a stationary body) the estimates collapse to `prev`/`curr`
and the blend degenerates to plain lerp/slerp.

### Extrapolation (t > 1)

Extrapolation is **disabled**.  When `t > 1` the interpolator holds `curr` pose.
At any meaningful velocity the per-frame displacement exceeds constraint tolerances,
producing visible joint popping.

### Correction Debug Lines

Yellow lines visualize the correction jump when a new server snapshot arrives:

1. Sample the interpolator at `render_time` **before** the push → `old_pos`.
2. Push the new snapshot.
3. Sample the interpolator at the **same** `render_time` after the push → `new_pos`.
4. Draw yellow lines from `old_pos` corners to `new_pos` corners, fading over 0.5 s.

This shows the actual instantaneous correction, not the natural travel distance.

---

## Video Capture

The optional video capture module (`fr_video_capture_t`) records the rendered
framebuffer to an MP4 file without stalling the render loop.

### Architecture

```
Render thread (GL)          CPU ring (SPSC)        Encode thread
  glReadPixels → PBO ──►  frame_ring_push() ──►  fwrite → ffmpeg pipe
  (4-slot PBO ring)         (8-slot ring)          (libx264, CRF 20)
```

- **PBO ring** (4 slots): async GPU→CPU readback via `GL_PIXEL_PACK_BUFFER`
  with fence sync.  Non-blocking — never stalls the render loop.
- **Frame ring** (8 slots, SPSC lock-free): transfers completed pixel buffers
  from render thread to encode thread.  Backpressure drops oldest frame.
- **Encode thread**: pipes raw RGBA to ffmpeg for H.264 encoding.  Falls back
  to raw `.raw` file if ffmpeg is not on PATH.

### Frame Decimation

The capture module decimates to the target FPS (`desc.fps`, default 30).
`submit_frame()` checks `CLOCK_MONOTONIC` and only initiates a PBO readback
when at least `1/fps` seconds have elapsed since the last capture.  This
produces exactly one unique frame per output video frame — no duplication,
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

## Phased Implementation

### Phase 1 — Physics test client (first priority)

Goal: spawn + movement correctness.
- I/O thread + demux (RUDP stream + raw UDP)
- decode SPAWN events and create entities with `body_id`
- decode BODY_STATE datagrams and push into pose interpolators
- render cubes + grid + collider wireframes

### Phase 2 — Interactive controller

- FPS mouse look + WASD
- send `INPUT_MOVE` / `INPUT_ROT` (queued to I/O thread)
- no prediction initially (server authoritative)

### Phase 3 — Full event coverage

- implement handlers for all event types (death/health/status/inventory)

### Phase 4 — Debug primitives expansion

- contact points, broadphase grid cells, constraint visualization

### Phase 5 — Assets + skinned meshes

- load meshes, render animated characters

---

## Failure Modes / Pitfalls

- **State before spawn:** drop or buffer briefly per `body_id`.
- **Protocol demux:** keep “RUDP vs raw UDP” detection explicit.
- **Backpressure:** spawn bursts must be batched; reliable stream must not silently drop.
- **Thread ownership:** never call socket APIs from the main thread.
