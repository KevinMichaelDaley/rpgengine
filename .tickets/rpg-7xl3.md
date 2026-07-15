---
id: rpg-7xl3
status: closed
deps: [rpg-9ont]
links: []
created: 2026-07-13T05:09:58Z
type: task
priority: 2
assignee: KMD
parent: rpg-w1qe
---
# render_material_t: PBR texture set + scalar params

CPU-side PBR material: handles for diffuse/albedo, normal, metalness, roughness, AO dirtmap, emissive (self-shading), + lightmap atlas ref; scalar params tint color, specular strength, roughness min/max remap, metalness scale. Binds its textures+uniforms for a draw.

## Design

Core renderer. Depends on texture_t. Metallic-roughness convention. Emissive texture flagged self-shading-only.

