---
id: rpg-h7c8
status: closed
deps: [rpg-d8l8, rpg-5t38, rpg-dtrg]
links: []
created: 2026-07-04T20:39:25Z
type: task
priority: 0
assignee: KMD
parent: rpg-npzr
tags: [procgen, grammar, registry, tdd, integration]
---
# procgen-3d: P3 integration test

rpg-npzr

## Design

Integration test: create two grammar stubs (mock_grammar_a, mock_grammar_b), register both, select each via @grammar header, verify correct rasterizer called. Test registry overflow. Test that blockout grammar works through the registry path. Verify no regressions in existing tokenize/rasterize pipeline.

## Acceptance Criteria

- Multiple grammars registered and selectable\n- Grammar switching works correctly\n- Blockout grammar works through registry\n- Registry error cases handled\n- No regressions in existing tests\n- RED-GREEN-REFACTOR complete

