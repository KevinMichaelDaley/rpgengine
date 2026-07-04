---
id: rpg-27m7
status: open
deps: [rpg-gc2a, rpg-gh4v, rpg-rvtw]
links: []
created: 2026-07-04T20:41:08Z
type: task
priority: 0
assignee: KMD
parent: rpg-gc2a
tags: [procgen, integration, e2e]
---
# procgen-9a: End-to-end pipeline test

## Design

Write a single test that exercises the entire pipeline: (1) Provide a user prompt to the architect with mock VLM → (2) Get token string → (3) Tokenize → (4) Rasterize → (5) Serialize to JSON → (6) Load JSON into engine → (7) Run 2 critic playthroughs with mock NitroGen → (8) Generate summary report. Verify each stage produces correct output. Verify no data loss at any stage boundary. Use the blockout grammar as the reference.

## Acceptance Criteria

- Full pipeline completes without errors\n- Each stage output verified\n- No data loss at stage boundaries\n- Report contains expected sections\n- Works with blockout grammar\n- Test is self-contained (no external dependencies)

