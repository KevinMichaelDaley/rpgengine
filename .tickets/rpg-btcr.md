---
id: rpg-btcr
status: closed
deps: [rpg-a3dm]
links: []
created: 2026-07-05T06:25:47Z
type: task
priority: 0
assignee: KMD
parent: rpg-85a0
tags: [procgen]
---
# srd-003: procgen_ascii_parse.c -- flood-fill + adjacency extraction

**DO NOT CREATE STUBS OR PARTIAL IMPLEMENTATIONS; NEVER SKIP FEATURES OR DEFER STEPS!**



Implement the ASCII grid parser. Parse multi-floor ASCII (with === FLOOR N: ... === headers) into RoomGraph. RED-phase: tests/procgen/procgen_ascii_parse_tests.c

## Acceptance Criteria

Correct node/edge/stair count for 2-floor test; handles edge cases

