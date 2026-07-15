---
id: rpg-tqr1
status: closed
deps: []
links: []
created: 2026-07-13T05:10:22Z
type: task
priority: 2
assignee: KMD
parent: rpg-zket
---
# Light entity types + scene light store

First-class light entities in the core renderer: point, spot, directional, area. Fields: position, direction, color, intensity, range/radius, spot inner/outer cone, and a realtime-vs-baked flag (baked lights feed the lightmap baker; realtime lights feed the pipeline). A scene light store the pipeline and editor consume.

## Design

Core renderer + scene interface. Reconcile with the baker's lm_light_t (point/dir/spot) so baked and realtime share a definition.

