---
id: rpg-w1qe
status: closed
deps: []
links: [rpg-1gj9]
created: 2026-07-13T05:09:19Z
type: epic
priority: 1
assignee: KMD
---
# PBR material + shader (Cook-Torrance, lightmap-aware)

Physically-based material and shader for the core renderer. Metallic-roughness workflow with a Cook-Torrance BRDF. Consumes the baked lightmap (SH9 irradiance atlas from rpg-1gj9) as the diffuse indirect term and evaluates the current pass's punctual lights for direct. Full texture set: diffuse/albedo map, normal map, metalness map, roughness map (+ roughness min/max remap params), AO 'dirtmap', and an emissive texture used ONLY for self-shading modulation (actual emissive LIGHTING is baked into the lightmap). Extra params: tint color and specular strength. Outputs shaded color.

## Design

Lives entirely in the core renderer module (src/renderer, include/ferrum/renderer) for reuse; NONE of it embedded in demo_client. Needs a texture_t abstraction (currently spec-only) and a render_material_t (texture handles + scalar params). Shader is GLSL (inline sources like existing shaders) wired via shader_program_t + shader_uniform_cache_t. SH lightmap is directional: evaluate SH irradiance against the per-pixel normal-mapped normal (uv1 samples the atlas). Follow TDD + extreme modularity (<=2 public types/header, <=4 non-static fns/.c).

## Acceptance Criteria

A renderable with the full texture set + params renders with correct PBR response: lightmap diffuse GI + punctual direct, normal mapping, metalness/roughness, AO, tint, specular strength, and emissive self-shading. Visual test renders a material sphere/plane and matches a reference. Clean under -Wall -Wextra -Wpedantic. No renderer code in demo_client.

