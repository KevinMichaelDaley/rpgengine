---
id: rpg-7u32
status: closed
deps: [rpg-gc2a]
links: []
created: 2026-07-04T20:41:08Z
type: task
priority: 0
assignee: KMD
parent: rpg-gc2a
tags: [procgen, integration, performance]
---
# procgen-9d: Performance benchmarks

## Design

Implement performance benchmarks for the procgen pipeline: (1) tokenize: time to parse strings of varying lengths (10/100/1000 tokens), (2) rasterize: time to convert tokens to layout for varying complexity, (3) serialize: time to convert layout to JSON for varying entity counts, (4) critic: throughput of playthroughs per minute with mock NitroGen. Measure on reference hardware. Store benchmarks for regression detection. Write RED benchmark tests that assert max times.

## Acceptance Criteria

- Tokenize: < 1ms for 100-token string\n- Rasterize: < 10ms for 100-token string\n- Serialize: < 5ms for 10-room dungeon\n- Critic: > 20 playthroughs/minute with mock agent\n- Benchmarks reproducible on reference hardware\n- REGRESSION: if any metric degrades > 2x, test fails\n- Benchmark results saved to file for comparison

