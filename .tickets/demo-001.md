---
id: demo-001
status: in_progress
deps: [phys-400]
links: []
created: 2026-02-06T14:17:00.000000000-08:00
type: epic
priority: 2
---
# Interactive Networked Physics Demo (Boxes + Spheres)

## Overview

A graphical, networked, multi-client demo of the Ferrum physics engine.
Players run around an open ground plane in first-person (WASD + mouse-look),
place randomly sized/colored boxes with **E**, and fire a sustained impulse
beam with **left-click**. Large cubes and spheres rain from the sky in the
distance.  The server computes authoritative physics for all rigid bodies and
replicates state to clients; clients render with prediction and show red
debug correction lines when the server corrects a pose.

**4 clients. 1 server with 4 worker threads. Compiled with Tracy.**

---

## Architecture

```
┌──────────────────────────────────────────────────────────┐
│  CLIENT (×4)                                             │
│                                                          │
│  SDL2 window (1280×720)                                  │
│  ├─ Mouse-look → yaw/pitch camera                        │
│  ├─ WASD → movement vector (kinematic, no physics)       │
│  ├─ Left-click (held) → fire impulse ray                 │
│  ├─ E (press+release) → spawn box request                │
│  │                                                       │
│  Outbound (reliable):                                    │
│  ├─ INPUT_MOVE (wasd + yaw/pitch)                        │
│  ├─ INPUT_FIRE (ray origin + direction, held flag)       │
│  ├─ INPUT_SPAWN_BOX (random half-extents + color seed)   │
│  │                                                       │
│  Inbound (unreliable):                                   │
│  ├─ STATE_PHYS (quantized pos/rot/vel per body)          │
│  │                                                       │
│  Rendering:                                              │
│  ├─ For each body: lerp client-predicted pose → server   │
│  │   truth (phys_prediction_reconcile)                   │
│  ├─ Render box/sphere mesh at interpolated pose           │
│  ├─ Red debug lines: 8 cube corners between              │
│  │   predicted and corrected pose                        │
│  │   (fr_debug_correction_lines_cube)                    │
│  └─ Ground plane (large textured quad or grid lines)     │
└──────────────────────────────────────────────────────────┘
          │  UDP/RUDP  │
          ▼            ▲
┌──────────────────────────────────────────────────────────┐
│  SERVER (1 process, 4 fiber workers)                     │
│                                                          │
│  server_repl_server_pump():                              │
│  ├─ Receive client inputs (move, fire, spawn)            │
│  ├─ Apply kinematic move to player body                  │
│  ├─ Resolve impulse ray → find nearest body → apply      │
│  │   force (continuous while held)                       │
│  ├─ Spawn box body at player position + forward          │
│  │                                                       │
│  phys_world_tick():                                      │
│  ├─ Full 14-stage pipeline (broadphase → solve → ...)    │
│  ├─ Ground plane: static body with large box collider    │
│  ├─ Player bodies: kinematic (moved by input, not phys)  │
│  ├─ Spawned boxes/spheres: dynamic, full simulation      │
│  │                                                       │
│  Random spawner:                                         │
│  ├─ Every N ticks, spawn random large box or sphere      │
│  │   at random position 50-100m away, y=30-50m           │
│  │   (falls under gravity)                               │
│  │                                                       │
│  server_repl_server_tick():                              │
│  ├─ Encode phys_snapshot for changed bodies              │
│  ├─ Replicate to all connected clients                   │
│  └─ Throttle: ~20 body updates per client per tick       │
└──────────────────────────────────────────────────────────┘
```

---

## Controls

| Input | Action |
|-------|--------|
| **W/A/S/D** | Move forward/left/back/right (kinematic, relative to look direction) |
| **Mouse** | Look around (yaw + pitch, first-person) |
| **Left-click** (hold) | Fire continuous impulse beam forward — pushes hit bodies |
| **E** (press+release) | Place a randomly sized/colored box 2m in front of player |
| **Escape** | Quit |

---

## Visual Appearance

- **Ground plane**: large flat quad (100×100m), subtle grid or checkerboard
- **Boxes**: randomly sized (0.3–2.0m half-extents), randomly colored (from
  a hashed seed), rendered as solid cubes with simple diffuse lighting
- **Spheres**: randomly sized (0.5–3.0m radius), similar coloring, rendered
  as low-poly sphere meshes (icosphere or UV-sphere)
- **Players**: small colored capsule or box representing each player
- **Debug correction lines**: red wireframe connecting the 8 corners of
  the client-predicted box to the 8 corners of the server-corrected box;
  rendered whenever the server corrects a pose; fade after 0.35s (existing
  `fr_debug_correction_lines_cube()` system)
- **Impulse beam**: thin line from player to hit point (or max range)

---

## Physics Setup

### Ground Plane
- Static body at y=0
- Box collider: half-extents (50, 0.5, 50) — thick slab
- Friction: 0.6, restitution: 0.2

### Player Bodies (×4)
- Kinematic bodies (PHYS_BODY_FLAG_KINEMATIC)
- Box or capsule collider (1m tall, 0.4m wide)
- Moved directly by client input (no gravity, no collision response)
- Other bodies bounce off players

### Spawned Boxes
- Dynamic bodies, mass proportional to volume
- Box collider matching visual half-extents
- Spawned 2m in front of player on E press
- Initial velocity: slight forward toss (5 m/s forward, 3 m/s up)

### Random Distant Spawns
- Every 30-60 ticks (0.5-1.0 seconds), spawn 1 large body
- 50% box (half-extents 1.0–5.0m), 50% sphere (radius 1.0–3.0m)
- Position: random XZ 50-100m from origin, Y = 30-50m
- Falls under gravity, bounces, rolls, eventually sleeps

### Impulse Beam
- Raycast from player eye forward (camera direction)
- Max range: 50m
- If hit: apply impulse = beam_strength × dt × hit_normal to body
- Continuous while held (not per-frame spike)
- beam_strength ≈ 500 N (tunable)

---

## Network Messages (New)

### Client → Server

```c
// Player movement (sent every tick while moving)
typedef struct demo_input_move {
    uint16_t event_id;
    int16_t  yaw_snorm16;      // look yaw quantized
    int16_t  pitch_snorm16;    // look pitch quantized  
    uint8_t  move_flags;       // bit 0=W, 1=A, 2=S, 3=D
    uint8_t  action_flags;     // bit 0=fire, 1=spawn_box
} demo_input_move_t;           // 8 bytes

// Box spawn request (sent once on E release)
typedef struct demo_input_spawn {
    uint16_t event_id;
    uint16_t half_x_mm;        // half-extent X in mm
    uint16_t half_y_mm;        // half-extent Y in mm
    uint16_t half_z_mm;        // half-extent Z in mm
    uint32_t color_seed;       // hashed for RGB
} demo_input_spawn_t;          // 12 bytes
```

### Server → Client

Uses existing `phys_snapshot_encode()` for body state replication.
Additionally sends spawn messages via existing SPAWN_BATCH protocol
with body type (box/sphere) and visual parameters (extents, color).

---

## Compilation

```bash
make demo-001 TRACY=1
```

Produces:
- `build/demo_server` — headless server (4 workers)
- `build/demo_client` — graphical client (SDL2 + OpenGL + GLEW)

### Run

```bash
# Terminal 1: Server
./build/demo_server 40080 4 60    # port workers tick_hz

# Terminals 2-5: Clients
./build/demo_client 127.0.0.1 40080
./build/demo_client 127.0.0.1 40080
./build/demo_client 127.0.0.1 40080
./build/demo_client 127.0.0.1 40080
```

---

## Dependencies

This demo requires **Phase 4** (Tiered Simulation) so that the parallel
job pipeline and basic optimization stack are in place:

- **phys-400**: Phase 4: Tiered Simulation (includes parallel jobs from
  Phase 3, all Phase 2 colliders, and distance-based tier classification)

All Phase 1 infrastructure (tick function, snapshots, prediction,
impact events) and Phase 2 colliders (sphere, box, capsule) are already
complete.

> **Note:** This is the single canonical demo for the Ferrum engine.
> Subsequent tickets (e.g. demo-009) upgrade *this same demo* to use
> newer physics phases — they are not separate demos.

---

## What Already Exists (reuse, don't reimplement)

These systems are **done** and the demo wires them together:

- **Physics pipeline**: `phys_world_tick()` — full 14-stage pipeline ✅
- **Snapshot encoding**: `phys_snapshot_encode/decode()` ✅
- **Prediction/reconciliation**: `phys_prediction_reconcile()` — snap/blend ✅
- **Debug correction lines**: `fr_debug_correction_lines_cube()` — red wireframe ✅
- **Tracy profiling**: `TRACY=1` build flag, zone markers in server/fiber code ✅
- **Server replication**: `server_repl_server_pump/tick()` — client management,
  SPAWN_BATCH, STATE_CUBE broadcast, INPUT_ROT receive ✅
- **Client renderer**: SDL2+OpenGL window, cube VBO/VAO, shader pipeline,
  `fr_pose_interpolator_t` for smooth rendering, per-entity coloring ✅
- **Quantization**: `net_quantize_vec3_mm`, `net_quantize_quat_snorm16` ✅

---

## Sub-Tickets (to be created)

> All sub-tickets below are parts of **this single demo**.  They are
> not separate demos — they build incrementally on the same binaries
> (`demo_server` / `demo_client`).

### New code

1. **demo-002**: FPS camera controller (~300 lines, new file)
   - Camera struct: position, yaw, pitch, move speed, mouse sensitivity
   - `demo_camera_update(camera, mouse_dx, mouse_dy, wasd_flags, dt)`
   - Builds view matrix via `mat4_look_at` from yaw/pitch each frame
   - SDL relative mouse mode (`SDL_SetRelativeMouseMode`)
   - No camera code exists anywhere in the engine today

2. **demo-003**: Two new network message schemas (~300 lines)
   - `demo_input_move_t`: yaw/pitch (snorm16) + move_flags (WASD bits)
     + action_flags (fire/spawn bits). ~8 bytes on wire.
   - `demo_input_spawn_t`: half-extents (mm) + color_seed. ~12 bytes.
   - Encode/decode functions following existing `net_repl_input_rot`
     pattern in `src/net/replication/`
   - Existing INPUT_ROT schema is for rotation commands, not movement

3. **demo-004**: Server physics world + game logic (~400 lines)
   - Init `phys_world_t` with ground plane (static box collider),
     4 kinematic player bodies
   - Input handler: apply `demo_input_move_t` → update player body
     position (kinematic, camera-relative)
   - Impulse beam: when fire flag set, iterate bodies to find nearest
     along forward ray, apply impulse (no raycast stage exists yet —
     simple brute-force over active bodies is fine for 200 bodies)
   - Box spawner: on `demo_input_spawn_t`, create dynamic body with
     box collider at player position + 2m forward, toss velocity
   - Random distant spawner: every 30-60 ticks, spawn large box or
     sphere at random far position, high Y, falls under gravity

4. **demo-005**: Sphere mesh + ground plane visuals (~300 lines)
   - Generate icosphere vertex data (subdivision level 2, ~80 tris)
   - Ground plane: large quad with grid-line pattern or checkerboard
   - Impulse beam: single GL_LINES segment from camera to hit point
   - Only cubes exist as renderable geometry today

### Integration (wiring existing systems)

5. **demo-006**: Client main loop (~500 lines, new file)
   - Fork of `p008_renderer_client.c` structure but with:
     - FPS camera (demo-002) instead of fixed isometric
     - WASD/mouse/click/E input → `demo_input_move_t` sent each tick
     - Receive STATE → `phys_prediction_reconcile()` (existing)
     - Render bodies at reconciled poses (existing pose interpolator)
     - `fr_debug_correction_lines_cube()` on correction (existing)
     - Render sphere vs box mesh based on body shape type
   - This is the main `demo_client` binary

6. **demo-007**: Server main loop (~300 lines, new file)
   - Fork of `p008_net_repl_server.c` structure but with:
     - `phys_world_t` owned by server, ticked each frame
     - Receive `demo_input_move_t/spawn_t` via existing pump
     - Call `phys_world_tick()` (existing)
     - Encode `phys_snapshot_encode()` (existing) → broadcast
     - 4 fiber workers via existing job system
   - This is the main `demo_server` binary

7. **demo-008**: Smoke test (~200 lines)
   - Embedded server + 4 headless clients in one binary
   - Connect, spawn 10 boxes, fire impulse, wait 120 ticks
   - Verify: all clients received spawns, state updates flowing,
     no crashes, bodies came to rest on ground plane

### Upgrade (same demo, later phases)

8. **demo-009**: Upgrade demo to Phase 7 (Advanced Stability)
   - Same demo binaries, same gameplay, same network protocol
   - Wire in manifold point reduction, speculative contacts,
     position-level solve from Phase 7
   - Add stress-test scenario: 20-box tower that must stay stable
   - Add high-velocity projectile test (100 m/s, no tunneling)
   - Increase random spawn rate and body cap to stress new stability
   - Update perf targets: < 2ms physics tick at 300 active bodies
   - Depends on phys-700 (Phase 7: Advanced Stability)

---

## Acceptance Criteria

- [ ] 4 clients connect to 1 server simultaneously
- [ ] Players move with WASD + mouse-look in first-person
- [ ] Left-click fires continuous impulse beam that pushes bodies
- [ ] E places randomly sized/colored box that simulates correctly
- [ ] Large boxes/spheres rain from the sky in the distance
- [ ] Bodies stack, bounce, roll, and eventually sleep
- [ ] Server computes authoritative physics (full 14-stage pipeline)
- [ ] Client renders with pose interpolation + prediction
- [ ] Red debug correction lines visible during server corrections
- [ ] Ground plane prevents infinite falling
- [ ] Compiled with Tracy profiling (`TRACY=1`)
- [ ] Server runs 4 fiber workers for parallel job dispatch
- [ ] < 3ms physics tick at 200 active bodies on server
- [ ] < 16ms client frame time (60 FPS target)
- [ ] No crashes under normal gameplay for 5+ minutes
