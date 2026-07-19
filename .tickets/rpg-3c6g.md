---
id: rpg-3c6g
status: open
deps: []
links: [rpg-iuiy]
created: 2026-07-19T05:04:34Z
type: feature
priority: 1
assignee: KMD
---
# DDGI recurrent-irradiance probe update (replace multi-bounce SDF cone trace)


## Summary
Replace the per-probe MULTI-BOUNCE, multi-sample SDF cone trace (the dominant cost
of the probe update -- so heavy we can only refresh probes every ~16-32 frames)
with the standard DDGI recurrent-irradiance update: each probe casts SHORT
single-hit rays and, at each hit, gathers the ALREADY-BOUNCED irradiance from the
surrounding probes -- visibility-weighted by the octahedral Chebyshev depth we
ALREADY build per probe from the same SDF. Multi-bounce then emerges over frames
(this frame's gather feeds next frame's), for near-free. Cheaper update -> can
refresh far more often (fixing "update frequency too low by half").

## Current (to replace)
gi_probe_gpu.c CS kernel: per probe, cone-trace ~8 sphere directions, each doing
gi_bounces bounces marching the combined SDF, gathering direct light + static
irradiance + voxel albedo, accumulated into SH. Also builds the DDGI octahedral
(mean, meanSq) depth per probe (Chebyshev vis) and 3 SG lobes -- these stay.

## Design
Per probe, per sample direction (reuse the existing ray set):
1. SINGLE SDF march to the FIRST hit (no multi-bounce loop).
2. Bounce radiance at the hit = direct light there (existing direct_at) +
   INDIRECT gathered from the PROBE FIELD at the hit point:
   - Sample the surrounding probes' SH irradiance (trilinear over the probe grid),
     weighted by Chebyshev visibility (their octahedral depth) so occluded probes
     don't leak -- exactly the forward+ probe_vis/sh path, in the compute.
   - This carries all previous bounces (recurrent) -> multi-bounce over frames.
3. NEAR-FIELD fallback: if the hit is VERY CLOSE (probe interpolation too coarse
   there), sample the voxel albedo/static-irr directly for that direction instead
   of the probe-field gather.
4. Accumulate (direct + albedo*indirect) into this probe's SH (existing SH build),
   with the temporal EMA.

Notes / risks:
- Feedback stability: reading the probe field while writing it -> use last frame's
  SH (already r/w buffers + EMA damps it); clamp/energy-normalise to avoid runaway.
- Self-contribution: exclude/downweight the probe's own cell so it doesn't
  reinforce itself.
- Keep the depth (Chebyshev) + SG lobe build as-is; only the irradiance gather
  changes from deep-march to single-hit + probe-field sample.
- Bootstrap: first N frames still need a real light bounce (direct at hit) so the
  field has energy to recurse; static-irr volume already seeds it.

## Acceptance
- Probe update cost drops enough to refresh every few frames without the stutter/
  fps hit; measured min/MAX frame time improved at equal-or-better GI quality.
- Multi-bounce still visible (color bleed) via the recurrence, not per-ray marching.
- No light-leak (Chebyshev vis holds) and no feedback runaway (clamped + EMA).

## Deps
Builds on the staggered/checkerboard + hardware-depth work in rpg-iuiy.
