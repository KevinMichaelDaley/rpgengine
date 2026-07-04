---
id: rpg-rvtw
status: open
deps: [rpg-gc2a]
links: []
created: 2026-07-04T20:41:08Z
type: task
priority: 0
assignee: KMD
parent: rpg-gc2a
tags: [procgen, integration, grammar]
---
# procgen-9c: Multi-grammar switching test

## Design

Create a second grammar stub (mock_grammar_v2 with different token set). Register blockout + mock_grammar_v2 in registry. Test: (1) @grammar blockout v1 selects blockout, (2) @grammar mock v2 selects mock, (3) unknown grammar produces correct error, (4) switching grammars between two consecutive tokenize calls works. Verify no state leakage between grammar instances.

## Acceptance Criteria

- Two grammars registered and selectable\n- @grammar header switches correctly\n- Unknown grammar error is descriptive\n- No state leakage between grammars\n- Concurrent grammar use does not crash\n- Registry lookup is O(1) amortized

