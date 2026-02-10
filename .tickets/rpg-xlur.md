---
id: rpg-xlur
status: closed
deps: []
links: []
created: 2026-02-10T11:52:03Z
type: epic
priority: 1
assignee: KMD
---
# Full-stack networked demo: FPS client + box-rain server

End-to-end integration demo exercising the full engine pipeline. Two binaries
in `tests/examples/`: a headless server and a graphical client.

## Critical Constraint: REUSE ONLY

**Every subsystem must be driven by existing module APIs.** No reimplementation.
Use the actual physics tick runner, server tick loop, tick encoder, body state
broadcast, RUDP stream, client RX/TX runtime, prediction/reconciliation,
ghost table, snapshot delta, and renderer primitives. The demo is glue code
that wires these modules together — nothing more.

## Server (`tests/examples/demo_server.c`)

- **Physics world** via `phys_world_init()` with full `phys_world_config_t`
  (gravity, tier system, island coloring — all default settings).
- **Ground plane**: static box body `(100, 0.1, 100)` at origin via
  `phys_world_set_box_collider()`.
- **Box rain**: every 5 seconds, spawn a stack of 20–50 dynamic box bodies
  from `y=20..40` (randomized positions within a 10×10 area) via
  `phys_cmd_push()` into the command channel. Bodies use
  `phys_body_set_box_inertia()` and `phys_body_set_mass()`.
- **Tick runner**: `phys_tick_runner_init/start()` — the real fiber-based
  physics loop with all 15 stages (broadphase, narrowphase, XPBD solve,
  tier classify, etc.).
- **Server tick loop**: `fr_server_tick_loop_init/step()` at 60 Hz.
  `on_drain` pumps net runtime, `on_physics` is a no-op (tick runner is
  async), `on_encode` calls body state broadcast + tick encoder,
  `on_flush` flushes RUDP streams.
- **Networking**: `fr_server_net_runtime_create()` for UDP accept +
  per-client RUDP. Body state broadcast via
  `fr_server_body_state_broadcast_tick()`.
- **Spawn notification**: spawn callback pushes `SPAWN_BATCH` messages to
  newly-connected clients via reliable topic channel.

## Client (`tests/examples/demo_client.c`)

- **SDL2 + OpenGL window** (same pattern as `p008_renderer_client.c`):
  VAO/VBO cube mesh, shader program, perspective projection.
- **FPS camera**: WASD movement + mouse-look (SDL relative mouse mode).
  Kinematic capsule collider for the local player — position driven by
  input, not physics forces.
- **Networking**: `fr_client_rx_create/start()` + `fr_client_tx_create/start()`
  connected to server UDP socket.
- **Ghost table**: `net_ghost_table_init()` maps server body IDs → local
  entity slots.
- **State application**: pop `BODY_STATE` messages from RX, decode via
  `net_snapshot_delta_apply()`, update entity poses.
- **Prediction + reconciliation**: `net_predict_init()` +
  `net_predict_reconcile()` for the local player capsule.
- **Rendering**: for each ghost entity, compute MVP from body pose and
  `glDrawArrays(GL_TRIANGLES, 0, 36)`. Ground plane rendered as a scaled
  flat box. Debug correction lines via `fr_debug_correction_lines_cube()`.
- **Ground plane**: rendered client-side (static, no network sync needed).

## What This Proves

- Full physics pipeline (tiers, islands, broadphase, XPBD) under load
- Network replication of 100+ dynamic bodies
- Client-side prediction and server reconciliation
- Ghost table mapping across connect/disconnect
- Delta compression under continuous state changes
- FPS controls with kinematic character

