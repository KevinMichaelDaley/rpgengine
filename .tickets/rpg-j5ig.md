---
id: rpg-j5ig
status: open
deps: [rpg-btcr, rpg-9bbw, rpg-mfdj]
links: []
created: 2026-07-05T06:26:00Z
type: task
priority: 0
assignee: KMD
parent: rpg-fcyx
tags: [procgen]
---
# srd-012: procgen_srd_bridge.cpp -- C API entry point

**DO NOT CREATE STUBS OR PARTIAL IMPLEMENTATIONS; NEVER SKIP FEATURES OR DEFER STEPS!**



C-callable: srd_generate(ascii, budget, &rooms, &n_rooms, &corridors, &n_corridors, &stairs, &n_stairs). Wraps parser+optimizer. RED-phase: call on 2-room grid, verify non-zero output.

## Acceptance Criteria

Returns valid geometry for valid input; returns -1 for invalid; caller frees arrays

