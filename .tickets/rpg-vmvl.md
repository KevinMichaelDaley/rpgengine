---
id: rpg-vmvl
status: open
deps: [rpg-bdrv]
links: []
created: 2026-07-04T20:39:25Z
type: task
priority: 0
assignee: KMD
parent: rpg-6p4q
tags: [procgen, grammar, serializer]
---
# procgen-2c: Level load integration

## Design

Wire the serializer into the engine level-loading path. Add a procgen_load_level() function that: accepts a token string or layout, serializes to JSON, then calls edit_level_deserialize to load into the engine. Write RED integration test that loads a generated level and walks the entity tree verifying structure.

## Acceptance Criteria

- procgen_load_level() accepts token string input\n- Level loads via existing deserialize path\n- Entity tree structure verifiable\n- Works with existing edit_level_load semantics

