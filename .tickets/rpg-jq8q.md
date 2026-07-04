---
id: rpg-jq8q
status: open
deps: [rpg-cras]
links: []
created: 2026-07-04T22:53:53Z
type: task
priority: 0
assignee: KMD
tags: [procgen, integration, e2e, smoke, tdd]
---
# procgen-3.5: Massive E2E smoke test after registry

## Design

Comprehensive end-to-end smoke test after the grammar registry (P3). Register the blockout grammar via registry, use @grammar header to select it, tokenize + rasterize + serialize. Then register a second stub grammar, verify switching works. Test the full pipeline from grammar selection → token string → layout → JSON. Test error paths: unknown grammar, version mismatch, unregistered grammar. Stress: 50 different dungeon strings through the registry pipeline.

## Acceptance Criteria

- Blockout grammar works through registry\n- @grammar header selects correct grammar\n- Grammar switching works mid-session\n- Unknown grammar produces clear error\n- 50 dungeon strings all pass through registry\n- Full pipeline (grammar select → JSON) verified\n- No regressions in existing tests

