---
id: rpg-didw
status: open
deps: []
links: []
created: 2026-07-13T05:24:16Z
type: task
priority: 2
assignee: KMD
parent: rpg-2ejn
---
# Point-light shadow cubemap render (single/double cubemap depth)

Render depth for a shadow-casting movable point light into a cubemap (or dual-paraboloid) each frame, covering dynamic + static casters in range.

## Design

Core renderer. Cube depth target, 6-face (or 2-paraboloid) render, per-light.

