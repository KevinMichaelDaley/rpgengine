---
id: rpg-oxnh
status: closed
deps: [rpg-8sc6, rpg-fizd]
links: []
created: 2026-07-04T20:40:15Z
type: epic
priority: 0
assignee: KMD
tags: [procgen, critic, runtime, nitrogen, tdd]
---
# procgen: Phase 7 - Critic Runtime

rpg-8sc6

## Design

Build the critic runtime that runs N automated playthroughs of a generated dungeon level using the NitroGen agent. For each playthrough: reset physics world, spawn player at SPAWN, run game loop (render → NitroGen → action → physics step → check events via hooks). Collect: death positions/times, marker reach status, survival time, distance traveled. After N playthroughs, compute summary statistics: success rate (all markers reached), avg survival time, death heatmap, most lethal zone. The critic is grammar-agnostic — it works with any fr_dungeon_layout_t.

## Acceptance Criteria

- N playthroughs run automatically (configurable, default 10)\n- Each playthrough: player spawns at SPAWN, NitroGen controls, hooks fire\n- Death events recorded with position/time/cause\n- Marker reach events recorded per marker\n- Per-playthrough stats: survival time, distance, markers reached\n- Summary: success rate, avg survival, death heatmap centroid\n- Playthrough timeout: stops run after configurable limit\n- Reset between playthroughs: physics world cleaned, player respawned\n- Works headless (no display required)\n- Configurable via critic_config_t\n- Grammar-agnostic: uses only spawn + markers + nav_graph

