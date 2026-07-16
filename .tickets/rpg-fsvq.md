---
id: rpg-fsvq
status: open
deps: [rpg-w1qe, rpg-zket]
links: []
created: 2026-07-13T05:23:16Z
type: epic
priority: 2
assignee: KMD
---
# Cascaded shadow maps for stationary lights (static-baked + per-frame dynamic split)

Cascaded shadow mapping for STATIONARY lights. Each stationary light pre-generates its own cascaded shadow map covering every STATIC object once (baked/cached, not redrawn per frame). Every frame, only the relevant shadow-casting DYNAMIC objects are rendered into a separate, lower-resolution shadow map for that light. At shade time the two are co-sampled: depth-compare against both and take the nearer occluder, so static and dynamic shadows combine. The resulting shadow term is combined with the lightmap ambient in the material shader.

## Design

Core renderer module only; nothing in demo_client. CSM: per-light frustum-split cascades with stabilized ortho/persp matrices. Static map: render all static casters once per stationary light, cache the depth (persist/rebuild on scene change). Dynamic map: per-frame, low-res, only relevant dynamic casters. Shade: min(static_occluder, dynamic_occluder) depth test -> shadow factor; feed the material shader's shadow term (updates rpg-w1qe). TDD + extreme modularity.

## Acceptance Criteria

Stationary lights cast correct cascaded shadows with static geometry shadowed from a pre-generated map (no per-frame static redraw) and dynamic objects shadowed from a per-frame low-res map, co-sampled correctly. Shadow term combines with lightmap ambient in the material shader. Entirely in core renderer. Clean under -Wpedantic.


## Notes

**2026-07-16T06:07:21Z**

CSM substantially implemented this session (view-independent + PCSS soft shadows). Static-baked cascades + per-frame dynamic map co-sampled; integrated in pbr_shader. Cascades are VIEW-INDEPENDENT (casters classified by size/background, cached forever) since the map is static and the player roams. Soft shadows via PCSS on a plain R32F linear-depth map (EVSM tried and rejected). Verified on hall + zone_small colonnade. See memory feedback_csm_view_independent.
