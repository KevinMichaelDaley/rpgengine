---
id: rpg-lmph
status: open
deps: []
links: []
created: 2026-07-21T01:37:54Z
type: task
priority: 1
assignee: KMD
parent: rpg-k23d
tags: [renderer, perf, shader, gi]
---
# Merge the duplicated GI probe gathers in the PBR fragment shader

Section 3.2 (likely biggest shader-side win, est. 0.5-1.5 ms @1080p Iris-class). gi_probe_specular (pbr_shader.c:504-536) re-computes everything gi_probe_indirect2 (:433-467) just did for the same fragment: same wp_b, g, corner indices, ppos, and worst -- a second set of 8 probe_vis depth-array taps, plus up to 8*u_gi_spec_lobes*2 texelFetches (32 at default lobes=2) and 8 normalizes. Fix: merge into one 8-corner loop accumulating diffuse SH, SG lobes, and sky sharing one visibility weight. ~40-50 fetches + 8 normalizes saved per pixel, zero visual change. Also section 3.4: probe_sky (:419-421) samples a per-probe constant per corner (8 taps) -- precompute openness per probe in the GI update and branch it out when sky-AO is disabled.

## Acceptance Criteria

The probe diffuse + specular + sky gather share a single 8-corner loop and one visibility weight; per-pixel probe fetch count drops ~40-50 with no visual change.

