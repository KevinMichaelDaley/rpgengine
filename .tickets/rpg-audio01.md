---
id: rpg-audio01
status: closed
deps: [rpg-llm04]
links: [rpg-llm04]
created: 2026-05-03T20:00:00Z
type: bug
priority: 2
assignee: KMD
parent: rpg-llm04
tags: [audio, bug, attenuation, negative, clamp]
---
# Negative Audio Attenuation for Distance < 1.0

`npc_audio_graph_query` in `src/npc/audio/npc_audio_propagation.c:42` computes:
```c
atten = 20.0f * log10f(dist);
```
For `dist < 1.0`, `log10(dist)` is negative, producing negative attenuation (i.e., amplification). There is no lower clamp at 0 dB — only an upper clamp at 60 dB.

Example: `dist = 0.5` → `log10(0.5) = -0.301` → `atten = -6.02 dB`

## Fix
```c
atten = 20.0f * log10f(fmaxf(dist, 1.0f));
```
Floor the distance to 1.0 meter before computing attenuation, ensuring the result is always >= 0 dB.

## Acceptance
- [ ] Query at dist=0.1 returns attenuation ≥ 0
- [ ] Query at dist=0.5 returns attenuation ≥ 0
- [ ] Query at dist=10.0 returns correct attenuation unchanged
