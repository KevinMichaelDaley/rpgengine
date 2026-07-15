---
id: rpg-9ebf
status: open
deps: [rpg-son4, rpg-na3h, rpg-8ufi]
links: []
created: 2026-07-13T05:10:49Z
type: task
priority: 2
assignee: KMD
parent: rpg-mvmh
---
# Deferred additive accumulation shader (small-radius points) + blend

Per-tile deferred shading: for each covered pixel, accumulate the additive contribution of the tile's particle lights through the PBR BRDF (reading the thin G-buffer), and blend additively onto the forward+ image.

## Design

Core renderer. Depends on the BRDF core (rpg-son4), the thin G-buffer, and tile binning. Additive blend, no double-count with forward+ direct.

