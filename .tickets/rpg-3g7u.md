---
id: rpg-3g7u
status: open
deps: []
links: []
created: 2026-07-16T01:00:57Z
type: task
priority: 2
assignee: KMD
---
# CSM shadow anti-aliasing (PCF / filtering)


## Notes

**2026-07-16T01:01:05Z**

The realtime directional CSM sun shadows (render_forward dir cascades, used by hall_lit_dynamic LM_ONLY/CSM) have hard, aliased edges -- the raking column shadows across the floor show clear stair-stepping. Add anti-aliasing: PCF (multi-tap percentage-closer filtering) on the cascade sample, and/or higher-res cascades / normal-offset bias to reduce shimmer. Consider a blue-noise/rotated-poisson PCF kernel. Files: src/renderer/shadow_csm_*.c, the CSM sampling in src/renderer/pbr_shader.c. Surfaced 2026-07-15 while validating the massive-zone lightmap (baked indirect + realtime CSM direct sun looks correct, but shadow edges are jaggy).
