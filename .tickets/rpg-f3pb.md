---
id: rpg-f3pb
status: open
deps: []
links: []
created: 2026-07-21T01:38:36Z
type: task
priority: 3
assignee: KMD
parent: rpg-k23d
tags: [renderer, perf, shader]
---
# PBR shader micro-costs: hoist G_Smith k, pow->mul, tonemap to post

Sections 3.5 + 3.6. Light loop (pbr_shader.c:164-207): G_Smith recomputes k=(r+1)^2/8 per light (:166) though roughness is fixed per pixel -- hoist; normalize(ldir) per spot per pixel (:204) -> normalize on CPU in forward_plus_pack_light; pow(x,5) (:167) and pow(d/r,4) (:192) -> mul chains; light data t0..t3 fetched unconditionally (:604-605) -> fetch t0/t1, range/cone/NoL-cull, then t2/t3 only if contributing. Tonemap (:691-692): Reinhard + pow(color,1/2.2) = 3 pows/pixel baked before MSAA resolve -- use sqrt (gamma 2.0) on low tier, or move to the post pass. ~10-15 ALU/light/pixel + 3 pows saved.

## Acceptance Criteria

Per-light k is hoisted, pows are mul chains, non-contributing lights skip t2/t3 fetch, and tonemap is moved to post (or sqrt on low); no visible change.

