---
id: rpg-gh4v
status: open
deps: [rpg-gc2a]
links: []
created: 2026-07-04T20:41:08Z
type: task
priority: 0
assignee: KMD
parent: rpg-gc2a
tags: [procgen, integration, stress]
---
# procgen-9b: Stress test - 100 random user prompts

## Design

Generate 100 diverse user prompts describing different dungeon types (small crypt, large castle, cave network, tower, labyrinth, arena, etc.). For each prompt, run the architect (mock VLM returning pre-canned valid responses), tokenize, rasterize, and validate. Measure: parse success rate (target >95%), average tokenize time, average rasterize time, most common failure modes. Write results to a stress report.

## Acceptance Criteria

- 100 unique prompts generated\n- Parse success rate > 95%\n- Avg tokenize time < 1ms\n- Avg rasterize time < 10ms\n- All valid layouts pass geometry validation\n- Stress report generated\n- No memory leaks over 100 iterations\n- Test completes in < 5 seconds

