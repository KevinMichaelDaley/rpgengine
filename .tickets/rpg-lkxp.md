---
id: rpg-lkxp
status: closed
deps: []
links: []
created: 2026-07-13T05:23:51Z
type: task
priority: 2
assignee: KMD
parent: rpg-fsvq
---
# Shadow-map resource management (depth targets, atlas/array, per-light alloc)

Core-renderer shadow depth-target management: allocate/track depth textures (2D array / atlas) for static and dynamic shadow maps, per stationary light, at configurable resolutions (high-res static, low-res dynamic).

## Design

src/renderer. Depth formats, comparison samplers, array/atlas layout. Foundation for CSM.

