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
```

The OpenGL context stays on the main thread.
The I/O thread is the only thread allowed to touch sockets.

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
