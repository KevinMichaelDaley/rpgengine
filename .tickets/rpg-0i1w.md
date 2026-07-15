---
id: rpg-0i1w
status: closed
deps: [rpg-son4, rpg-9ont]
links: []
created: 2026-07-13T05:09:58Z
type: task
priority: 2
assignee: KMD
parent: rpg-w1qe
---
# Shader lightmap term: SH irradiance vs per-pixel normal + emissive self-shading

Sample the baked SH9 lightmap atlas (rpg-1gj9) via uv1 and evaluate irradiance against the per-pixel NORMAL-MAPPED normal (SH is directional, so it combines with the normal map), scaled by albedo/AO for the diffuse indirect term. Emissive texture used only as self-shading modulation, NOT as a light source (emissive lighting is already baked).

## Design

Core renderer. Depends on the BRDF core + texture_t. Needs the SH9 basis in GLSL (9 coeffs x 3 channels) or a prebaked irradiance encoding; decide encoding vs storage cost.

