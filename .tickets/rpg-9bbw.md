---
id: rpg-9bbw
status: open
deps: [rpg-4mwa, rpg-gc07, rpg-mfdj]
links: []
created: 2026-07-05T06:26:00Z
type: task
priority: 0
assignee: KMD
parent: rpg-zyxb
tags: [procgen]
---
# srd-011: srd_optimizer.cpp -- Newton loop + rewrite schedule + time budget

**DO NOT CREATE STUBS OR PARTIAL IMPLEMENTATIONS; NEVER SKIP FEATURES OR DEFER STEPS!**



SRD main loop: SymX Newton solver, propose rewrites every K steps, accept if loss improves >=1%, recompile on element changes, time budget enforcement. RED-phase: tests/procgen/srd/srd_smoke_tests.cpp

## Acceptance Criteria

2-room converges in 5s; 4-room produces connected geometry; budget enforced

