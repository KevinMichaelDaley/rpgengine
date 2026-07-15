---
id: rpg-ya6c
status: in_progress
deps: [rpg-lkxp]
links: []
created: 2026-07-13T05:23:51Z
type: task
priority: 2
assignee: KMD
parent: rpg-fsvq
---
# CSM cascade setup for stationary lights (frustum split + stabilized matrices)

Cascade splits over the view frustum and per-cascade light view/projection matrices with texel-snapping stabilization, for stationary directional/large lights.

## Design

Core renderer. Standard CSM: log/linear split blend, stabilized ortho bounds.

