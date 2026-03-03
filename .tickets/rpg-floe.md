---
id: rpg-floe
status: open
deps: [rpg-ifb2]
links: []
created: 2026-03-02T18:39:47Z
type: task
priority: 2
assignee: KMD
---
# Phase 6a: light component types and tiled light culling

Create light types (directional, point, spot) and CPU-side tiled light culling. See ref/renderer_spec.md §7.1-7.2.

Deliverables:
- include/ferrum/renderer/light/light_component.h: light_type_t enum (DIRECTIONAL/POINT/SPOT), light_component_t struct (type, color[3], intensity, range, inner/outer cone, cast_shadows, is_static)
- include/ferrum/renderer/light/light_cull.h: Tiled culling API — 16x16 pixel tiles, sphere-vs-frustum test per light per tile, output per-tile light index lists
- src/renderer/light/light_cull.c: CPU-side culling (extract tile frustum from projection + tile bounds, test each light, pack into SSBO-ready format)
- SSBO layout: per-tile struct with light_count + light_indices[MAX_LIGHTS_PER_TILE=64]
- Light SSBO (binding=0): packed Light struct array (position_type, direction_range, color_intensity, cone_angles)
- Tile index SSBO (binding=1): per-tile light index lists
- Forward shader reads tile from gl_FragCoord, iterates tile light list
- Max 1024 lights total
- Tests in tests/p004_renderer_light_cull_tests.c

Depends on: rpg-ifb2 (shader permutations for light count defines)

