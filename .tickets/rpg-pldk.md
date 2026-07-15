---
id: rpg-pldk
status: open
deps: [rpg-7xl3]
links: []
created: 2026-07-13T06:41:35Z
type: task
priority: 2
assignee: KMD
parent: rpg-dweu
---
# Material transparency attributes + render queue

Add alpha, blend mode, and transmission colour to the material, plus a render-queue tag (opaque vs transparent) so the pipeline can route and sort.

## Design

Core renderer. Extends render_material_t; defines queue/blend enums.

