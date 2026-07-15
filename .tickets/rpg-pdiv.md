---
id: rpg-pdiv
status: closed
deps: [rpg-son4]
links: []
created: 2026-07-13T05:09:58Z
type: task
priority: 2
assignee: KMD
parent: rpg-w1qe
---
# Shader punctual direct lighting for the current pass light set

Evaluate the current pass's punctual lights (point/spot/directional) through the PBR BRDF for the direct term. Attenuation, spot cones, N.L, shadowing hook. The lightmap supplies indirect; these supply realtime direct. Light data provided per-pass (interface finalized in the pipeline epic).

## Design

Core renderer. Depends on BRDF core. Loop over a bounded per-pass/per-cluster light set (clustering comes from rpg-zket).

