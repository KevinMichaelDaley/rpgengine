---
id: rpg-yyah
status: open
deps: []
links: []
created: 2026-07-21T01:36:33Z
type: task
priority: 1
assignee: KMD
parent: rpg-k23d
tags: [renderer, perf, shader, shadows]
---
# Sun-shadow sampling: NoL early-out + gate dynamic-shadow taps

Section 2.3. pbr_shader.c:594 calls pbr_csm_shadow unconditionally -- 25-57 taps/fragment paid even facing away from the sun. Fixes:
(a) float sunvis = (dot(N,sun) > 0.0) ? pbr_csm_shadow(...) : 0.0; -- free.
(b) skip pbr_dyn_shadow (always +9 taps at :350 via min(vis,...)) when vis < 1/8 and when a new u_dyn_active uniform is 0 (CPU knows the dynamic-caster count at bind time).
(c) knob dir_pcf_taps (8/4/1): 4-tap rotated Poisson is fine at 1024; 1 = hard shadows (also skips IGN kernel-rotation math at :588-590).
(d) keep dir_pcss=0 on low tiers; compile PCSS out of the LOW shader variant.
Also section 2.4: point-light cube sampling is 8 taps x shadowed light (pbr_shader.c:218-234) -- add shadow_pcf_taps (8/4/1).

## Acceptance Criteria

Fragments facing away from the sun skip all CSM taps; dynamic-shadow taps are skipped when already occluded or no dynamic casters exist; dir_pcf_taps/shadow_pcf_taps reduce tap counts including a 1-tap hard-shadow mode.

