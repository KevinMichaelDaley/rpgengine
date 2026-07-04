---
id: rpg-aqm2
status: open
deps: []
links: []
created: 2026-07-04T20:41:07Z
type: epic
priority: 1
assignee: KMD
tags: [procgen, critic, vlm, visual, tdd]
---
# procgen: Phase 8 - Visual Coherence VLM

rpg-oxnh

## Design

Implement the lightweight VLM visual coherence critique system. After each playthrough (or on death), capture screenshots at key positions (death point, last known position, marker locations) and send them to a small VLM (Qwen2.5-VL-3B or Gemma-3-4B) for critique. The VLM answers: 'Does this geometry look coherent? Are there visual glitches, z-fighting, missing textures, or impossible geometry?' Output: coherence score (0.0-1.0) + issue list. The coherence critique augments the critic's summary report, helping identify rendering and geometry problems that might affect playability.

## Acceptance Criteria

- Screenshots captured at death positions and marker locations\n- Screenshots sent to lightweight VLM via existing LLM infrastructure\n- VLM prompt asks about visual coherence: glitches, Z-fighting, missing textures, impossible geometry\n- Coherence score (0.0-1.0) returned\n- Issue list returned (human-readable)\n- Critique integrated into critic summary report\n- Works with small open-weight VLMs (Qwen2.5-VL-3B, Gemma-3-4B)\n- Screenshot capture works headless (offscreen rendering)

