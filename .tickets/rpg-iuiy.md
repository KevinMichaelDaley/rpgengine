---
id: rpg-iuiy
status: in_progress
deps: []
links: [rpg-tpcg, rpg-3c6g]
created: 2026-07-19T03:10:28Z
type: task
priority: 1
assignee: KMD
---
# Instrument + optimize the runtime GI (staggered probe updates)


## Summary
Instrument and optimize the runtime GI + newly-added great-hall geometry. The
dressed hall renders ~24 fps (~42 ms/frame) on the Iris Xe; the biggest lever is
the per-update probe cone-trace, which currently re-traces ALL probes (1200) in a
single compute dispatch every `update_interval` frames. Amortize that by
STAGGERING probe updates — trace a rotating subset each frame — and add Tracy
zones so we can see where the time actually goes before tuning further.

## Current state (grounded)
- `gi_probe_gpu_dispatch` (src/renderer/gi/gi_probe_gpu.c) dispatches the compute
  kernel over ALL `g->n_probes` at once; each probe cone-traces the resident SDF
  chunks (now 8) + analytic collider boxes, builds SH (dyn+static), DDGI octahedral
  depth, and 3 SG lobes.
- `gi_runtime.c` already gates the whole dispatch behind `update_interval`
  (frame_counter % interval) and applies temporal EMA (`u_temporal`, 0.25) so
  results smooth over time. No per-probe staggering yet — it's all-or-nothing.

## Plan
### Instrumentation (do first)
- Tracy zones (`Render.GI.*`): probe dispatch, SDF stream/residency
  (gi_sdf_stream), vis prepass, per-forward-pass probe sampling; plus GPU timer
  queries around the compute dispatch so we get GPU-side ms, not just CPU submit.
- Count + log the new geometry's draw cost (archivolt, responds, plinths, niches,
  frieze added ~100 meshes) — check it isn't a separate regression.
- Capture a baseline trace driving the noclip cam; record probe dispatch ms, SDF
  upload ms, forward+ ms.

### Staggered probe updates (main optimization)
- Split the probe set into K groups; each frame dispatch only group `frame % K`.
  Full refresh over K frames, ~1/K the per-frame cost.
- **DITHER the group assignment across the probe ARRAY / grid** -- do NOT use
  contiguous slices (those sweep a staleness "wavefront" through space, leaving a
  whole region stale for K-1 frames). Assign each probe to a group by a spatially
  DECORRELATED function of its 3D grid coords -- e.g. a Bayer/ordered-dither
  pattern, bit-reversal interleave, or `(x*p1 ^ y*p2 ^ z*p3) % K` -- so every
  frame's updated subset is scattered across the whole volume and every big region
  gets at least a few probes refreshed each frame.
- Keep the temporal EMA; scale the per-group `u_temporal` so a probe updated every
  K frames still converges at the same wall-clock rate (blend ∝ frames since its
  last update).
- Prioritize: bias the stagger so probes near the camera / recently-moved dynamic
  colliders (the sliding cubes) refresh more often than distant static ones
  (importance-ordered groups, not pure round-robin).
- Make K (and update_interval) tunable via env for A/B.

## Acceptance
- Tracy shows per-stage GI GPU ms; a baseline vs. optimized trace is captured.
- Probe dispatch cost per frame drops ~1/K with no visible popping (temporal EMA +
  per-group blend scaling) while driving.
- Frame time on the Iris Xe improves measurably at the same visual quality; K is
  env-tunable.
- Near-camera / dynamic-object probes still respond promptly (no K-frame lag on the
  moving cubes' bounce).

## Notes

**2026-07-19T03:23:18Z**

PROFILING (great hall, Iris Xe, 1080p, driving): baseline 1200 probes no-stagger 21.6 fps; stagger K=8 18.2 (REGRESSED - per-frame dispatch overhead, mostly glGetUniformLocation x8); 160 probes 21.2; 72 probes 21.9 (probe count ~irrelevant). MSAA 8x->off only +2.5 fps. => probe trace is NOT the bottleneck; the forward PBR fragment shading dominates (SH9 lightmap IBL + 3 SG lobes + DDGI + CSM PCSS + 5 cube shadows/fragment). Stagger+dither implemented + defaulted OFF (GI_PROBE_GROUPS to enable). NEXT: instrument+optimize the forward pass (cache probe-dispatch uniform locations; consider trimming per-fragment GI work / SG lobe count / shadow sample counts; MSAA knob GH_MSAA added).

**2026-07-19T03:43:05Z**

OUTCOMES: (1) SG lobes 3->2 default: +14% fps (22.9->26.2), minimal quality loss (1 lobe lost too much); GI_SG_LOBES env. (2) Octahedral depth moved SSBO/samplerBuffer -> RG32F 2D-array (GL_LINEAR, one layer/probe); probe_vis is now 1 hardware bilinear tap vs 4 texelFetch+manual mix; GI verified correct (no seams/leaks/diamonds). Marginal fps on this scene (~1fps, within +-3 noise) but correct architecture + scales with probe count/res. (3) Staggered+dithered probe updates implemented, DEFAULT OFF (regressed - probes aren't the bottleneck). (4) GH_MSAA knob. Forward PBR shading remains the bottleneck; SG lobe count was the single biggest lever found.

**2026-07-19T03:58:34Z**

DEPTH PREPASS + OVERDRAW: a depth pre-pass already exists (fwd_depth_submit, LEQUAL forward, no discard -> early-Z auto). Added PBR_NOPREPASS to A/B it: prepass ON ~26 fps vs OFF 17 fps = +50%! Occluded-fragment shading (each running the full 8-corner probe trilinear = 'probes sampled by occluded geo') was the cost the prepass kills. Added PBR_OVERDRAW: additive-blend GL_ALWAYS heatmap (debug mode 11, constant per fragment) -> overdraw hotspots are the roof trusses (overlapping thin beams) + geometry edges/intersections. Prepass depth buffer is available for postprocessing.
