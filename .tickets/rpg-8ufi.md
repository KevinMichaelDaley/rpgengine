---
id: rpg-8ufi
status: open
deps: [rpg-tqr1]
links: []
created: 2026-07-13T05:10:49Z
type: task
priority: 2
assignee: KMD
parent: rpg-mvmh
---
# Particle-light screen-tile binning (per-tile light lists)

Bin small-radius particle/point lights into screen-space tiles, producing per-tile light index lists that drive the deferred accumulation pass. Cheap culling for thousands of tiny lights.

## Design

Core renderer. Depends on light entities. Screen tiles (e.g., 16x16 px); light->tile assignment via screen-space bounds of each light's radius.

