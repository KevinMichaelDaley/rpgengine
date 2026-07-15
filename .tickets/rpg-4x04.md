---
id: rpg-4x04
status: closed
deps: [rpg-y5p7]
links: []
created: 2026-07-13T05:10:22Z
type: task
priority: 2
assignee: KMD
parent: rpg-zket
---
# Depth pre-pass

Z-only pre-pass over opaque renderables to populate the depth buffer for clustered culling and to reduce forward+ overdraw.

## Design

Core renderer. New pass in the render_pipeline_* graph. Depends on the scene submission interface.

