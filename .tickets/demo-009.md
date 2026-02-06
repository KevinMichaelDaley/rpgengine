---
id: demo-009
status: open
deps: [phys-700, demo-001]
links: []
created: 2026-02-06T14:54:51Z
type: task
priority: 2
assignee: KMD
---
# Upgrade Demo to Phase 7 (Advanced Stability)

## Overview

This is **not a separate demo** — it upgrades the existing demo-001
binaries (`demo_server` / `demo_client`) to use the Phase 7 advanced
stability features.  Same gameplay, same controls, same network
protocol.  The only changes are under the hood in the physics pipeline
and in new stress-test scenarios that exercise the improvements.

## What Changes

### Physics pipeline wiring
- Manifold point reduction (phys-701) is automatic once the stage
  exists — no demo code changes needed
- Speculative contacts (phys-702): enable in `phys_world_t` config
  (set `speculative_margin` > 0)
- Position-level solve / split impulse (phys-703): enable in world
  config (set `use_split_impulse = true`)

### New stress-test scenarios
- **20-box tower**: spawn a 20-high tower of identical boxes at game
  start.  Must remain stable for 1000+ ticks (no drift > 1mm,
  no collapse, clean planar sleep propagation)
- **High-velocity projectile**: fire a small sphere at 100 m/s.
  Must collide with a wall of boxes without tunneling through any.
  Validates speculative contacts.
- Increase random distant spawn rate (every 15-30 ticks instead of
  30-60) and raise body cap to 400 active bodies

### Updated perf targets
- < 2ms physics tick at 300 active bodies on server
- < 1ms physics tick at 200 active bodies (down from < 3ms)
- Client frame time still < 16ms (60 FPS)

### Smoke test update (demo-008)
- Add tower stability assertion (20-box tower, 500 ticks, < 1mm drift)
- Add projectile no-tunnel assertion (100 m/s sphere vs wall)

## What Does NOT Change
- Client/server binary names
- Network message schemas (demo_input_move_t, demo_input_spawn_t)
- FPS camera, WASD movement, mouse-look
- Left-click impulse beam, E to spawn box
- Visual appearance, rendering pipeline
- Tracy profiling integration
- 4-client / 4-worker topology

## Dependencies
- **phys-700**: Phase 7 (Advanced Stability) — manifold reduction,
  speculative contacts, position-level solve
- **demo-001**: The base demo must exist first

