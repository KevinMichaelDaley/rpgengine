---
id: rpg-9u96
status: in_progress
deps: []
links: [rpg-jj8j]
created: 2026-07-21T01:36:33Z
type: task
priority: 1
assignee: KMD
parent: rpg-k23d
tags: [renderer, perf, shadows]
---
# Cube shadows: cull, cache, budget, and clear only refreshed slots

Section 2.1 -- the single worst per-frame cost (3 review passes converged). Today every FLAG_SHADOW point light re-renders the ENTIRE uncalled scene x6 faces every frame (shadow_cube.c:181-193), after a full clear of all 6*shadow_max layers (~25 MB/frame at 8x256, shadow_cube.c:148-155) that runs even with zero shadow-flagged lights.
Fixes in order:
1. Per-light caster cull: skip renderables with dist(AABB, light_pos) > light.range (range already in render_light_t).
2. Cache stationary lights: per-slot dirty flag; re-render only on light move or dynamic-caster AABB intersecting the range sphere (mirror CSM static_valid).
3. Clear only refreshed slots (glFramebufferTextureLayer / scissored layered clear); skip the block entirely when no light has FLAG_SHADOW.
4. Knobs (from the config ticket): shadow_update_interval (round-robin N slots/frame), shadow_distance (drop slots beyond X m; sort candidates by camera distance); slot assignment nearest-to-camera instead of first-come (render_forward.c:321-330).
5. Hoist glBindFramebuffer/glViewport/program bind out of the per-light loop (shadow_cube.c:165-172).
6. Spot lights currently take the 6-face cube path (render_forward.c:323) -- 6x waste for a cone.

## Acceptance Criteria

Stationary shadowed lights in a static scene cost ~0 per frame; out-of-range casters are skipped; clears touch only refreshed slots; shadow_update_interval/shadow_distance bound the per-frame cube-shadow cost.


## Notes

**2026-07-21T02:20:35Z**

PART 1 DONE (commit 6b2e99ee): (1) caster range-cull -- shadow_cube_render_light(range) skips casters beyond a light's reach via new sphere_cull_aabb; (2) skip the whole cube block + full-array clear when no light wants a shadow; (3) shadow_distance knob threaded render_config->render_forward_config_t. Behavior-preserving: hall render pixel-identical (0.00/255). sphere_cull.c + cull_internal.h added (4-fn rule); frustum_cull_tests 9/9.
PART 2 REMAINING (this ticket stays open): per-slot dirty-flag caching of stationary lights (re-render only on light move / dynamic-caster intersection), clear-only-refreshed slots (glFramebufferTextureLayer), shadow_update_interval round-robin, hoist FBO/viewport/program bind out of the per-light loop, nearest-to-camera slot assignment. These need per-slot persistent state on shadow_cube_t.
