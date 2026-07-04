---
id: rpg-5npl
status: open
deps: [rpg-py13, rpg-xlnl, rpg-q3dv]
links: []
created: 2026-07-04T20:37:58Z
type: task
priority: 0
assignee: KMD
parent: rpg-o9fl
tags: [procgen, grammar, tdd, integration]
---
# procgen-0d: P0 integration test

rpg-o9fl

## Design

End-to-end test for Phase 0: tokenize a complete dungeon string (rooms, corridors, ramps, spawn, markers, nesting) and verify all tokens emitted correctly. Test roundtrip fidelity. Test boundary cases: empty string, whitespace-only, maximum nesting depth, maximum parameter value ranges. Write RED test first, then implement integration harness.

## Acceptance Criteria

- Full dungeon string tokenizes without errors\n- Token count matches expected\n- Token types and values verified\n- Edge cases handled gracefully\n- Build integration: make test builds and runs procgen tests

