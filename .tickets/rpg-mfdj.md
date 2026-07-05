---
id: rpg-mfdj
status: open
deps: [rpg-a3dm, rpg-r82r, rpg-btcr]
links: []
created: 2026-07-05T06:48:36Z
type: task
priority: 1
assignee: KMD
parent: rpg-t6ia
tags: [procgen]
---
# srd-017: srd_loss_compiler.cpp — parse LOSS: expression → energy tree

DO NOT CREATE STUBS OR PARTIAL IMPLEMENTATIONS; NEVER SKIP FEATURES OR DEFER STEPS!

Parse the VLM-emitted LOSS: block (a mini-DSL) into a tree of loss primitive call nodes. Example input:
LOSS:
  PathDistance(entrance, treasure) > 30
  LineOfSight(guard_room, treasure)
  NonPenetration(all)

Produces an expression tree where each leaf is a primitive call with resolved room label references, combined via weighted sum. Handles weight= modifiers and operator specifiers (<, >, =, default > for distance constraints).

RED-phase: tests/procgen/srd/srd_loss_compiler_tests.cpp — parse valid LOSS expressions, verify correct primitive count, correct label resolution, handle parse errors gracefully.

## Acceptance Criteria

Correctly parses all 10 primitive types; resolves room labels from ASCII annotations; rejects malformed LOSS blocks with clear error; builds valid SymX energy expression

