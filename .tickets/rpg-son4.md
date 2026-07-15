---
id: rpg-son4
status: closed
deps: []
links: []
created: 2026-07-13T05:09:58Z
type: task
priority: 2
assignee: KMD
parent: rpg-w1qe
---
# GLSL Cook-Torrance PBR BRDF core (GGX/Smith/Fresnel, normal map, AO, remap, tint, specular)

The fragment BRDF: Cook-Torrance specular (GGX distribution, Smith visibility, Schlick Fresnel), Lambert diffuse, metallic-roughness mixing, tangent-space normal mapping (TBN), AO modulation, roughness min/max remap, tint color, specular strength. Vertex shader with TBN + uv0/uv1 varyings.

## Design

Core renderer, inline GLSL sources. Metallic-roughness workflow. Correct linear-space math + energy conservation.

