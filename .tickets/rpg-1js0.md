---
id: rpg-1js0
status: open
deps: [rpg-wkky, rpg-1gj9]
links: []
created: 2026-07-13T05:24:17Z
type: task
priority: 2
assignee: KMD
parent: rpg-hrb6
---
# Probe generation from lightmap/baker data

Generate each probe's SH9 irradiance from the baked scene, reusing the lightmap baker's SVO + luxel radiance (gather incident radiance at probe positions).

## Design

Core renderer + reuse src/lightmap. Offline/preprocess like the lightmap bake.

