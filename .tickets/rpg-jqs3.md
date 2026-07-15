---
id: rpg-jqs3
status: open
deps: [rpg-lkxp]
links: []
created: 2026-07-13T05:23:51Z
type: task
priority: 2
assignee: KMD
parent: rpg-fsvq
---
# Per-frame dynamic-caster low-res shadow map for stationary lights

Each frame, render only the relevant shadow-casting DYNAMIC objects for each stationary light into a separate, lower-resolution shadow map (culled to the light + cascade).

## Design

Core renderer. Depends on shadow resources. Low-res, dynamic-only, per frame.

