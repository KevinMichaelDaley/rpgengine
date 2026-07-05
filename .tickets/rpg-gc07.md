---
id: rpg-gc07
status: open
deps: [rpg-a3dm, rpg-ndhj]
links: []
created: 2026-07-05T06:26:00Z
type: task
priority: 1
assignee: KMD
parent: rpg-nlev
tags: [procgen]
---
# srd-010: srd_rewrite.cpp -- apply rewrite specs to element sets

**DO NOT CREATE STUBS OR PARTIAL IMPLEMENTATIONS; NEVER SKIP FEATURES OR DEFER STEPS!**



Given rewrite proposals, modify RoomGraph element sets. Handle index invalidation. RED-phase: verify element counts after each rewrite type.

## Acceptance Criteria

All rewrite types produce correct element counts; connections stay consistent

