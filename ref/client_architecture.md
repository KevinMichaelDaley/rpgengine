# Client Architecture

## Overview

The game client is an SDL2 + OpenGL application that connects to the
authoritative server, receives reliable events and unreliable state
updates, interpolates entity poses, and renders the world.  It also
captures player input and sends it to the server for authoritative
processing.

The client reuses existing modules wherever possible:

| Concern                 | Module                                          | Status      |
|-------------------------|-------------------------------------------------|-------------|
| RUDP receive / ACK      | `fr_client_rx_t` (RX thread)                   | Exists      |
| Reliable stream reassem | `fr_rudp_stream_t` (stream_io)                 | Exists      |
| Pose interpolation      | `fr_pose_interpolator_t`                        | Exists      |
| Shader / VAO / VBO      | `shader_program_t`, `vao_t`, `vbo_t`           | Exists      |
| Skinning pipeline       | `skinning_pipeline_t`                           | Exists      |
| Render graph            | `render_pipeline_t` (skybox → forward → post)  | Exists      |
| Debug lines             | `fr_debug_lines_t`                              | Exists      |
| Math                    | `vec3_t`, `quat_t`, `mat4_t`, SLERP            | Exists      |
| ECS                     | `ecs_world_t`, sparse sets                      | Exists      |
| Player camera/controller| —                                               | **To build**|
| Event decoder           | —                                               | **To build**|
| Debug primitives        | —                                               | **To build**|
| Asset loading           | —                                               | **To build**|

## Assumptions

- The client connects to exactly one server.
- The server is authoritative; the client does not simulate physics.
- The client renders at display refresh rate and interpolates between
  server snapshots (no client-side prediction initially).

---

## Client Frame Loop

```
┌──────────────────────────────────────────────────────────┐
│                      CLIENT FRAME                        │
│                                                          │
│  1. Poll SDL events + capture input   (main thread)      │
│  2. Drain network messages            (main thread)      │
│  3. Process reliable events           (main thread)      │
│  4. Apply unreliable state updates    (main thread)      │
│  5. Update player controller + camera (main thread)      │
│  6. Send input to server              (main thread)      │
│  7. Interpolate entity poses          (main thread)      │
│  8. Build render lists                (main thread)      │
│  9. Execute render pipeline           (main thread)      │
│ 10. Swap buffers                      (main thread)      │
└──────────────────────────────────────────────────────────┘
```

The client is single-threaded for rendering (OpenGL context) with a
separate RX thread for network receive (`fr_client_rx_t`).

### Stage 1 — Poll SDL Events

```c
SDL_Event ev;
while (SDL_PollEvent(&ev)) {
    switch (ev.type) {
    case SDL_QUIT:         running = false; break;
    case SDL_KEYDOWN:      input_key_down(ev.key.keysym.scancode); break;
    case SDL_KEYUP:        input_key_up(ev.key.keysym.scancode); break;
    case SDL_MOUSEMOTION:  input_mouse_move(ev.motion.xrel, ev.motion.yrel); break;
    case SDL_MOUSEBUTTONDOWN: input_mouse_down(ev.button.button); break;
    }
}
```

Relative mouse mode (`SDL_SetRelativeMouseMode(SDL_TRUE)`) for FPS
look control.  Key state is accumulated into an `input_state_t` struct
that persists across the frame.

### Stage 2 — Drain Network Messages

The RX thread (`fr_client_rx_t`) runs continuously, receiving RUDP
packets, sending ACKs, and reassembling reliable stream frames.  The
main thread drains decoded messages via:

```c
uint8_t buf[MAX_MSG];
size_t len = sizeof(buf);
while (fr_client_rx_pop_message(rx, channel, buf, &len)) {
    client_dispatch_message(schema_id, buf, len);
    len = sizeof(buf);
}
```

Two categories of messages arrive:

| Channel    | Content                        | Delivery    |
|------------|--------------------------------|-------------|
| Reliable   | `server_event_t` (stream)      | Ordered     |
| Unreliable | `BODY_STATE` / `STATE_CUBE`    | Latest-wins |

### Stage 3 — Process Reliable Events

Reliable events arrive in-order via the RUDP stream.  The client
decodes the `server_event_type_t` tag and switches:

```c
server_event_t event;
server_event_decode(buf, len, &event);
switch (event.type) {
case SERVER_EVENT_SPAWN:
    client_entity_spawn(world, &event);     /* Create ECS entity + pose interp */
    break;
case SERVER_EVENT_DESPAWN:
    client_entity_despawn(world, &event);   /* Destroy ECS entity              */
    break;
case SERVER_EVENT_DEATH:
    client_entity_death(world, &event);     /* Trigger death FX / ragdoll      */
    break;
case SERVER_EVENT_HEALTH:
    client_entity_health(world, &event);    /* Update HP bar                   */
    break;
case SERVER_EVENT_STATUS:
    client_entity_status(world, &event);    /* Apply/remove status VFX         */
    break;
case SERVER_EVENT_INVENTORY:
    client_entity_inventory(world, &event); /* Update inventory UI             */
    break;
}
```

**Spawn** creates an ECS entity, allocates a `fr_pose_interpolator_t`,
and registers it in the render world.  **Despawn** removes it.

### Stage 4 — Apply Unreliable State Updates

For each `BODY_STATE` message, decode and push into the corresponding
entity's pose interpolator:

```c
net_repl_body_state_t bs;
net_repl_body_state_decode(buf, len, &bs);
fr_pose_interpolator_t *interp = entity_get_interp(world, bs.body_id);
if (interp) {
    vec3_t pos = net_repl_vec3_mm_to_float(&bs.pos_mm);
    quat_t rot = net_repl_quat_decode(&bs);
    fr_pose_interpolator_push(interp, now_s, pos, rot);
}
```

State updates are fire-and-forget; the interpolator handles gaps
via modest extrapolation (clamped to 1.5× interval).

### Stage 5 — Update Player Controller + Camera

The player controller translates raw input into a movement vector
and look direction:

```c
typedef struct fps_controller {
    vec3_t  position;
    float   yaw, pitch;          /* Accumulated from mouse delta       */
    float   move_speed;          /* Units/sec                          */
    bool    grounded;
    /* Key state */
    bool    forward, back, left, right, jump, crouch;
} fps_controller_t;
```

Each frame:

1. Accumulate yaw/pitch from mouse deltas (with pitch clamp).
2. Build a forward/right basis from yaw.
3. Compute a move vector from WASD state × basis × dt.
4. Write the move vector + look direction into an `input_move_t`
   and `input_rot_t` for sending to the server.
5. Derive the view matrix from controller position + yaw/pitch.

The controller is **client-side prediction only** — the server applies
the authoritative position.  Initially no reconciliation; the client
simply tracks the server's authoritative position for its own body.

### Stage 6 — Send Input to Server

Pack the current move + rotation into the existing schemas and send
reliably:

```c
net_repl_input_move_encode(&move, buf, &len);
net_rudp_peer_send_reliable(peer, sock, addr, now_ms,
                            NET_REPL_SCHEMA_INPUT_MOVE, buf, len);

net_repl_input_rot_encode(&rot, buf, &len);
net_rudp_peer_send_reliable(peer, sock, addr, now_ms,
                            NET_REPL_SCHEMA_INPUT_ROT, buf, len);
```

Input is sent every frame at a capped rate (e.g., 60 Hz or match
server tick rate).

### Stage 7 — Interpolate Entity Poses

For every entity with a pose interpolator, sample at the current
render time (which lags behind receive time by one snapshot interval
to allow smooth interpolation):

```c
double render_time = now_s - snapshot_interval_s;
for (each entity with interp) {
    vec3_t pos; quat_t rot;
    if (fr_pose_interpolator_sample(interp, render_time, QUAT_EPS, &pos, &rot)) {
        entity_set_transform(world, entity, pos, rot);
    }
}
```

### Stage 8 — Build Render Lists

Walk the ECS world and collect drawable entities into render lists:

- **Skinned meshes**: query entities with `skinning_skin_t` component →
  feed into `skinning_pipeline_update()` → `skinning_pipeline_query_draw_list()`.
- **Static meshes**: query entities with a mesh + transform component →
  build a simple draw list (model matrix + VAO + vertex count).
- **Debug primitives**: collect from debug subsystems (see below).

### Stage 9 — Execute Render Pipeline

The existing render pipeline runs passes in dependency order:

```
Skybox → Forward → Post
```

The forward pass submits:
1. Static mesh draw calls (instanced where possible).
2. Skinned mesh draw calls (bone palette bound per skeleton).
3. Debug primitive draw calls (lines, wireframes, grids).

### Stage 10 — Swap Buffers

```c
SDL_GL_SwapWindow(window);
```

---

## Debug Rendering (Feature: Debug Primitives)

The existing `fr_debug_lines_t` provides timed line segments.  This
needs to be extended into a full debug primitive system for physics
visualization and development.

### Planned Debug Primitive Types

| Primitive       | Use Case                                    | Draw Mode   |
|-----------------|---------------------------------------------|-------------|
| Line segment    | Rays, traces, velocity vectors              | `GL_LINES`  |
| Wireframe box   | AABB / OBB colliders, broadphase cells      | `GL_LINES`  |
| Wireframe sphere| Sphere colliders, interaction radii          | `GL_LINES`  |
| Wireframe capsule| Capsule colliders, character bounds         | `GL_LINES`  |
| Grid            | World-space ground plane, editor grid        | `GL_LINES`  |
| Point           | Contact points, body centers of mass         | `GL_POINTS` |
| Triangle (wire) | Mesh collider faces, convex hull faces       | `GL_LINES`  |

### Debug Draw API (Proposed)

```c
/** Immediate-mode debug draw — accumulated per frame, flushed at render. */
typedef struct debug_draw {
    vec3_t  *vertices;       /* Interleaved pos+color (6 floats each) */
    size_t   vertex_count;
    size_t   vertex_capacity;
    vao_t    vao;
    vbo_t    vbo;
    shader_program_t shader; /* Simple pos+color passthrough          */
} debug_draw_t;

void debug_draw_line(debug_draw_t *dd, vec3_t a, vec3_t b, vec3_t color);
void debug_draw_wire_box(debug_draw_t *dd, vec3_t center, vec3_t half, quat_t rot, vec3_t color);
void debug_draw_wire_sphere(debug_draw_t *dd, vec3_t center, float radius, vec3_t color);
void debug_draw_wire_capsule(debug_draw_t *dd, vec3_t a, vec3_t b, float radius, vec3_t color);
void debug_draw_grid(debug_draw_t *dd, float spacing, int half_count, vec3_t color);
void debug_draw_point(debug_draw_t *dd, vec3_t pos, float size, vec3_t color);

/* Called once per frame after accumulation, before pipeline execute */
void debug_draw_flush(debug_draw_t *dd);
/* Called after render to reset for next frame */
void debug_draw_clear(debug_draw_t *dd);
```

This is an **immediate-mode** API: callers add primitives each frame,
`debug_draw_flush()` uploads to GPU, the forward pass draws them, and
`debug_draw_clear()` resets for the next frame.  No persistent state
beyond the current frame (except the GPU resources).

The existing `fr_debug_lines_t` (with TTL-based expiration) remains
for persistent debug visuals like network correction cubes.

### Debug Draw Compile Gate

All debug draw calls should be gated by a compile-time flag so they
can be stripped from release builds:

```c
#ifdef DEBUG_DRAW_ENABLE
    debug_draw_wire_box(&dd, body_pos, body_half, body_rot, (vec3_t){0,1,0});
#endif
```

---

## Phased Implementation

The client should be built incrementally.  Each phase produces a
testable, runnable milestone.

### Phase 1 — Physics Test Client (First Priority)

**Goal:** Connect to server, receive body spawns + state, render
colored cubes with interpolation.  Verify the full network → render
pipeline end-to-end.

**Scope:**
- SDL2 window + GL 3.3 context
- FPS camera (mouse look + WASD fly-through, no physics)
- Connect via `fr_client_rx_t`, receive RUDP
- Decode `SERVER_EVENT_SPAWN` → create entity with hardcoded cube mesh
- Decode `BODY_STATE` → push into `fr_pose_interpolator_t`
- Render cubes at interpolated transforms (single forward pass)
- Ground grid via `debug_draw_grid()`
- Wireframe collider overlay via `debug_draw_wire_box()`

**Not in scope:** skinned meshes, asset loading, player movement
input, death/health/inventory events, post-processing.

**Modules reused:** `fr_client_rx_t`, `fr_rudp_stream_t`,
`fr_pose_interpolator_t`, `shader_program_t`, `vao_t`, `vbo_t`,
`ecs_world_t`, `fr_debug_lines_t`, math library.

### Phase 2 — Player Controller + Input

**Goal:** Add FPS controller with mouse look and WASD, send
`INPUT_MOVE` / `INPUT_ROT` to server, see own body move.

**Scope:**
- `fps_controller_t` implementation
- Input capture + relative mouse mode
- Encode and send input schemas at tick rate
- Client tracks own body via server state (no prediction)

### Phase 3 — Reliable Event Handling

**Goal:** Handle the full `server_event_type_t` enum.

**Scope:**
- Despawn: remove entity from ECS + render world
- Death: trigger placeholder FX (e.g., flash red, fade out)
- Health/status/inventory: store in ECS components (UI display
  deferred to a later phase)

### Phase 4 — Debug Primitives

**Goal:** Full `debug_draw_t` system with compile gate.

**Scope:**
- All primitive types listed above
- Physics visualization: collider wireframes, contact points,
  velocity vectors, broadphase grid cells
- Toggle layers via key binds (F1 = colliders, F2 = contacts, etc.)

### Phase 5 — Asset Loading + Skinned Meshes

**Goal:** Load meshes from disk, render skinned characters.

**Scope:**
- Simple binary mesh format (positions, normals, UVs, bone weights)
- Mesh registry keyed by asset ID
- Integrate `skinning_pipeline_t` for skeletal animation
- Spawn events carry an asset ID; client looks up mesh

### Phase 6 — Static Geometry + Environment

**Goal:** Load and render world geometry (terrain, structures).

**Scope:**
- Static mesh instances placed by world data
- Basic frustum culling
- Skybox pass integration

---

## Existing Modules Reused

| File                                                  | Role in client                        |
|-------------------------------------------------------|---------------------------------------|
| `src/net/client/runtime_rx_*.c`                       | RX thread: receive, ACK, reassemble  |
| `src/net/rudp/stream/stream_io.c`                     | Reliable stream frame reassembly     |
| `src/net/replication/interp/pose_interpolator.c`      | Snapshot interpolation (LERP + SLERP)|
| `src/renderer/shader_program.c`                       | Shader compile / bind                |
| `src/renderer/vao.c`, `vbo.c`                         | Vertex buffer management             |
| `src/renderer/render_pipeline*.c`                     | Multi-pass render graph              |
| `src/renderer/skinning/*.c`                           | Skeletal animation pipeline          |
| `src/renderer/debug_lines/*.c`                        | TTL-based debug line segments        |
| `src/ecs/world.c`                                     | Entity registry                      |
| `include/ferrum/net/replication/body_state.h`         | Wire format decode                   |
| `include/ferrum/net/replication/common.h`             | Schema IDs                           |
| `include/ferrum/math/*.h`                             | vec3, quat, mat4, SLERP              |

## What Needs to Be Built

| Item                    | Phase | Notes                                          |
|-------------------------|-------|-------------------------------------------------|
| Client main loop        | 1     | SDL init, frame loop, GL context                |
| Event decoder           | 1     | `server_event_decode()` + dispatch switch       |
| Cube renderer           | 1     | Hardcoded unit cube VAO, per-entity model matrix|
| FPS controller          | 2     | `fps_controller_t` with mouse + WASD            |
| Input encoder + sender  | 2     | Pack + send `INPUT_MOVE` / `INPUT_ROT`          |
| Full event handlers     | 3     | Despawn, death, health, status, inventory       |
| Debug draw system       | 4     | `debug_draw_t` immediate-mode API               |
| Asset loader            | 5     | Binary mesh format + registry                   |
| Skinning integration    | 5     | Wire up `skinning_pipeline_t` to draw list      |
| Static world geometry   | 6     | World mesh instances + frustum cull             |
