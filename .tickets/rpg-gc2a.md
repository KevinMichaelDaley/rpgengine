---
id: rpg-gc2a
status: open
deps: [rpg-o9fl, rpg-bdrv, rpg-6p4q, rpg-npzr, rpg-uzd4, rpg-8sc6, rpg-fizd, rpg-oxnh]
links: []
created: 2026-07-04T20:41:08Z
type: epic
priority: 0
assignee: KMD
tags: [procgen, integration, e2e, tdd]
---
# procgen: Phase 9 - Integration + End-to-End Tests

rpg-oxnh rpg-8sc6

## Design

Final integration phase: end-to-end tests covering the entire pipeline from architect VLM → token string → tokenize → rasterize → serialize → load level → critic playtest → report. Includes stress tests (100 random user prompts), multi-grammar tests, performance benchmarks, and smoke tests that run in CI. All phases (P0-P8) must be complete before starting P9.

## Acceptance Criteria

- Full pipeline: architect generates string → rasterizes → loads → critic plays → report\n- Stress test: 100 random prompts, >80% parse on first attempt\n- Multi-grammar: switch grammars at runtime, correct output\n- Performance: tokenize < 1ms, rasterize < 10ms per 100-token string\n- Critic: 10 playthroughs complete in < 5 minutes (headless, mock agent)\n- All existing procgen tests pass (no regressions)\n- Build integration: make test includes procgen tests\n- Zero memory leaks (valgrind clean on test suite)\n- CI smoke test: full pipeline in < 2 minutes\n- Documentation: README for procgen subsystem

