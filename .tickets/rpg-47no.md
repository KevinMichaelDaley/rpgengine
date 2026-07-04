---
id: rpg-47no
status: open
deps: [rpg-dltg, rpg-023m, rpg-vmvl]
links: []
created: 2026-07-04T20:39:25Z
type: task
priority: 0
assignee: KMD
parent: rpg-6p4q
tags: [procgen, grammar, serializer, tdd, integration]
---
# procgen-2d: P2 integration test

rpg-6p4q

## Design

Full roundtrip test: token string → tokenize → rasterize → serialize to JSON → load JSON → verify entity tree matches original geometry. Test corner cases: empty dungeon, single-room dungeon, multi-floor dungeon. Test JSON output against schema expectations.

## Acceptance Criteria

- Full roundtrip: string → JSON → entities works\n- All geometry types survive serialization intact\n- Entity tree matches layout geometry\n- Corner cases handled gracefully\n- RED-GREEN-REFACTOR complete

