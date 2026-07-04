---
id: rpg-cras
status: closed
deps: [rpg-6p4q]
links: []
created: 2026-07-04T22:53:53Z
type: task
priority: 0
assignee: KMD
tags: [procgen, integration, smoke, tdd]
---
# procgen-2.5: Mid-way integration smoke test

## Design

Integration test covering all completed subsystems (P0-P2). Tokenize a full dungeon string → rasterize layout → serialize to JSON → verify roundtrip fidelity. Test multiple dungeon variations, edge cases, and verify JSON output is structurally valid. This gate validates that tokenizer, rasterizer, and serializer work together correctly.

## Acceptance Criteria

- Full pipeline: string → tokens → layout → JSON works end-to-end\n- Multiple dungeon variations produce valid JSON\n- JSON contains all expected entity types\n- Entity IDs are sequential\n- Roundtrip: layout data survives serialization intact\n- Edge cases: empty rooms, max count entities, deep nesting\n- 100% test pass rate across all procgen tests

