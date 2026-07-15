---
id: rpg-h4ez
status: open
deps: [rpg-wkky, rpg-1js0]
links: []
created: 2026-07-13T05:24:17Z
type: task
priority: 2
assignee: KMD
parent: rpg-hrb6
---
# Runtime probe sampling + interpolation for dynamic objects

Sample and trilinearly interpolate nearby probes to produce per-object (or per-pixel) SH for a dynamic object, ready to evaluate against the surface normal.

## Design

Core renderer. Handle probe occlusion/leaking minimally for now.

