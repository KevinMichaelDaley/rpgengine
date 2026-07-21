---
id: rpg-0v3q
status: open
deps: []
links: []
created: 2026-07-21T01:36:33Z
type: task
priority: 2
assignee: KMD
parent: rpg-k23d
tags: [renderer, perf, shadows]
---
# CSM dynamic map: skip when no dynamic casters; fit to dynamic AABBs

Section 2.2. shadow_csm_render.c:157-182: even with zero dynamic casters the pass binds, clears 1024 R32F + depth (~8 MB/frame of clears), and unbinds. shadow_csm_cascade.c:255-258 ortho-fits the single dynamic map to the WHOLE scene AABB (coarse texels AND fixed cost). Fixes: skip the pass and set u_dyn_enabled=0 when dynamic_from == count; fit the ortho to the union of dynamic-caster AABBs; optional dir_dynamic_interval knob (30 Hz shadows for slow movers are invisible).

## Acceptance Criteria

The dynamic CSM pass is skipped (and sampling disabled) when there are no dynamic casters; the ortho fits dynamic casters; dir_dynamic_interval bounds its update rate.

