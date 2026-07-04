---
id: rpg-wksd
status: open
deps: [rpg-8sc6]
links: []
created: 2026-07-04T20:40:15Z
type: task
priority: 0
assignee: KMD
parent: rpg-oxnh
tags: [procgen, critic, runtime, playthrough]
---
# procgen-7a: Single playthrough runner

## Design

Implement critic_run_playthrough() in critic/critic_runtime.c. Set up physics world from fr_dungeon_layout_t. Spawn player at SPAWN position. Main loop: render frame → write to NitroGen shm → read action → apply to player → step physics → check hooks → repeat until death/timeout/markers complete. Track: frame count, elapsed time, player trajectory. Write RED test with a simple 2-room layout.

## Acceptance Criteria

- Player spawns at correct SPAWN position\n- Game loop runs: render → NitroGen → action → physics\n- Playthrough ends on death\n- Playthrough ends on timeout\n- Playthrough ends on all markers reached\n- Frame count and elapsed time recorded\n- Works headless (no GPU required for this task)

