---
id: rpg-62o7
status: closed
deps: []
links: [rpg-kapu, rpg-iuxn]
created: 2026-03-02T00:40:54Z
type: feature
priority: 2
assignee: KMD
tags: [aegis, gameplay, scripting, testing]
---
# Turret script: end-to-end gameplay perf/smoke test

Write an Aegis bytecode script that implements a turret gun as an end-to-end smoke/perf test of the scripting system.

**Turret behavior:**
1. Each tick, the turret queries nearby entities (using ENTITY_QUERY ops) to find the player.
2. Line-of-sight check: submit a VIS_TEST async raycast from the turret origin toward the player. If the ray hits something before reaching the player, LOS is blocked — turret does nothing.
3. Range check: if player distance > turret range, turret does nothing.
4. If player is visible AND in range:
   a. Compute a damped rotation toward the player (using vec3/quat math ops).
   b. Push a rotation UPDATE for the turret entity.
   c. SIGNAL a 'trace_projectile!' event with payload containing the turret entity ID, player entity ID, and fire direction.
5. The turret script subscribes to a periodic 'turret_tick' event (or self-re-arms via EXIT + re-trigger).

**What this tests end-to-end:**
- Entity queries (ENTITY_QUERY, ENTITY_GET_ATTR)
- Async raycasts (VIS_TEST → POLL/AWAIT)
- Vec3/quat math (distance, normalize, quat_from_to, slerp)
- Update pushes (UPDATE_PUSH for rotation)
- Event signaling (SIGNAL 'trace_projectile!')
- Idle/exit lifecycle (EXIT → re-arm on next event)
- Full runtime: loader, VM execution, async drain, entity snapshot, event queue

**Test structure:**
- Write in Aegis assembly (.asm) using the assembler
- Compile, register with runtime, spawn entities, fire events, tick multiple frames
- Assert: turret rotates toward player, trace_projectile event fires, no crashes under sustained load
- Perf: time 1000 turret ticks, ensure < 50µs average per script tick

**Dependencies:** All existing aegis subsystems (VM, async, entity, events, runtime)

## Acceptance Criteria

- Turret script compiles from .asm and loads into runtime
- LOS raycast fires and blocks shooting when obstructed
- Turret rotation updates are pushed to entity
- trace_projectile! event is signaled with correct payload
- Perf benchmark: 1000 ticks < 50ms total
- No memory leaks, no crashes, clean shutdown

