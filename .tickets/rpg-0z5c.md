---
id: rpg-0z5c
status: closed
deps: [rpg-7xl3, rpg-0i1w, rpg-pdiv]
links: []
created: 2026-07-13T05:09:58Z
type: task
priority: 2
assignee: KMD
parent: rpg-w1qe
---
# PBR shader program wrapper + uniform wiring + visual test

Assemble the PBR shader program (shader_program_t + shader_uniform_cache_t), wire all texture units + uniforms from render_material_t + camera + lights, and a visual/unit test rendering a material sphere/plane that matches a reference.

## Design

Core renderer. Depends on A1-A5. Reusable entry point the pipeline calls per renderable.

