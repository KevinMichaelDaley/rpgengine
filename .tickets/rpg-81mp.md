---
id: rpg-81mp
status: open
deps: [rpg-oxnh, rpg-nhjk, rpg-q6x7, rpg-1tgj]
links: []
created: 2026-07-04T20:41:07Z
type: task
priority: 0
assignee: KMD
parent: rpg-aqm2
tags: [procgen, critic, vlm, integration]
---
# procgen-8d: Coherence score integration into critic summary

## Design

Wire the visual coherence critique into the critic runtime. After each playthrough: collect screenshots, send to VLM, receive scores + issues, store in per-playthrough results. Compute aggregate: avg_coherence across all playthroughs, most common visual issue. Include coherence data in the summary report. Write RED test with mock VLM responses.

## Acceptance Criteria

- Coherence scores stored per playthrough\n- Avg coherence computed across playthroughs\n- Visual issues aggregated in summary\n- Coherence data appears in report\n- Mock VLM integration verified\n- No performance impact on playthrough (async capture)

