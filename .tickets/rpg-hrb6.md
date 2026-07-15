---
id: rpg-hrb6
status: open
deps: [rpg-w1qe, rpg-1gj9]
links: []
created: 2026-07-13T05:23:16Z
type: epic
priority: 2
assignee: KMD
---
# SH irradiance probes for dynamic-object ambient (generated from lightmap)

SH irradiance probes providing ambient/indirect lighting for DYNAMIC objects (which have no baked lightmap). A volume/grid of SH9 probes is generated from the lightmap data (reuse the baker's SVO + luxel radiance), and dynamic objects sample interpolated nearby probes for their ambient term in the material shader.

## Design

Core renderer module only; nothing in demo_client. Probe storage: grid/volume of SH9 (9 coeffs x 3 channels) probes. Generation: evaluate irradiance SH at each probe from the baked scene (share the baker's gather/SVO). Runtime: trilinear probe interpolation -> per-object or per-pixel SH, evaluated against the surface normal for ambient. Feeds the material shader's ambient term for dynamic objects (parallels the static lightmap term). TDD + extreme modularity.

## Acceptance Criteria

Dynamic objects receive plausible ambient GI from SH probes generated off the lightmap, sampled and evaluated against the shaded normal, matching nearby static-lightmap ambient. Probe generation reuses the baker. Entirely in core renderer. Clean under -Wpedantic.

