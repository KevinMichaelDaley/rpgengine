---
id: rpg-ry2l
status: open
deps: [rpg-8sc6, rpg-wksd]
links: []
created: 2026-07-04T20:40:15Z
type: task
priority: 0
assignee: KMD
parent: rpg-oxnh
tags: [procgen, critic, runtime]
---
# procgen-7c: Multi-playthrough runner

## Design

Implement critic_run_all() that orchestrates N playthroughs. For each: reset physics world, spawn player, run playthrough, collect events, store per-playthrough results in critic_runtime_t.results array. Track aggregate stats incrementally. Handle NitroGen process lifecycle (start once, reuse across playthroughs). Write RED test with 3 playthroughs on a test layout.

## Acceptance Criteria

- N playthroughs run sequentially\n- Each playthrough starts from clean physics world\n- Per-playthrough results stored\n- NitroGen process reused across playthroughs\n- Early termination on consecutive successes or failures\n- Progress output during runtime

