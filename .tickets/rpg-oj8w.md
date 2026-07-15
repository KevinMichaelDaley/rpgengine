---
id: rpg-oj8w
status: open
deps: [rpg-f5m4]
links: []
created: 2026-07-13T06:41:34Z
type: task
priority: 2
assignee: KMD
parent: rpg-nt4y
---
# Full-depth self-shadow raymarch toward the light

A separate raymarch from the displaced surface point toward each light through the heightfield using the FULL depth range, producing a self-shadow/occlusion factor -- decoupled from the shallow display depth so shadows read deep without stretching the silhouette.

## Design

Core renderer, PBR shader. Cone/SDF-accelerated shadow march at full height scale; soft penumbra optional.

