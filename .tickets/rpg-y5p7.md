---
id: rpg-y5p7
status: closed
deps: [rpg-tqr1]
links: []
created: 2026-07-13T05:10:22Z
type: task
priority: 2
assignee: KMD
parent: rpg-zket
---
# Scene submission interface (renderables, camera, lights -> pipeline)

The interface the app/editor uses to submit a frame: a render_scene_t / draw-list of renderables (mesh handle + render_material_t + transform), the camera, and the light list. The pipeline consumes this; demo_client only builds one and calls the pipeline.

## Design

Core renderer. Depends on light entities and render_material_t (rpg-7xl3). Handle-based, no per-frame malloc (arena/pool).

