---
id: rpg-iuiy
status: open
deps: []
links: []
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
- Split the probe set into K round-robin groups; each frame dispatch only group
  `frame % K` (a contiguous/strided slice via a base-offset + count uniform, or a
  per-probe last-update stamp). Full refresh over K frames, ~1/K the per-frame
  cost.
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
