---
id: rpg-lvgz
status: open
deps: [rpg-floe]
links: []
created: 2026-03-02T18:40:02Z
type: task
priority: 2
assignee: KMD
---
# Phase 6b: PSSM shadow maps for directional lights

Implement Parallel-Split Shadow Maps for directional lights with static light optimization. See ref/renderer_spec.md §7.3.

Deliverables:
- include/ferrum/renderer/light/shadow_map.h: Shadow map types — PSSM config (4 cascades: 0-10m@2048, 10-30m@2048, 30-100m@1024, 100-500m@1024), per-cascade FBO + depth texture, light-space matrices
- src/renderer/light/shadow_pssm.c: PSSM cascade computation (split distances, light-space matrix per cascade), render scene from light perspective into cascade FBOs
- src/renderer/light/shadow_point.c: Point light cube map shadows (6-face, 512x512 per face)
- src/renderer/light/shadow_spot.c: Spot light 2D shadow maps (1024x1024)
- Static light optimization: immobile directional lights render shadow maps once at caster stage, cache until dynamic object enters/exits cascade frustum or light direction changes
- Shadow map sampler: sampler2DArray for PSSM cascades, samplerCube for point lights, sampler2D for spot lights
- Per-frame UBO includes cascade_splits (vec4) and light_space[4] (mat4 array)
- Max 8 shadow map renders per frame (4 PSSM + 4 dynamic lights)
- Tests in tests/p004_renderer_shadow_tests.c

Depends on: rpg-floe (tiled culling determines which lights need shadows)

