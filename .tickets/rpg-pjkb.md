---
id: rpg-pjkb
status: open
deps: []
links: []
created: 2026-07-21T05:33:20Z
type: task
priority: 1
assignee: KMD
---
# Optimize probe placement + gather sampling efficiency


The probe system's placement + sampling are at WORST-CASE efficiency and the perf
problems compound from it:

1. **Placement is a dense uniform lattice** (probe_place_grid over the scene AABB,
   currently 23x11x12 = 3036 for the great hall at probe_spacing_scale 0.7). Most of
   those probes are in open air mid-hall or buried in masonry (relocation rescues the
   embedded ones' CONTENT but they are still lattice slots). Probes should be placed
   adaptively: surface-adjacent shells + interior volumes, pruned from solid and from
   featureless open air, importance-weighted where the GI gradient is steep
   (wall/floor contacts, openings, light sources). The trilinear grid path in the
   forward pass assumes a dense lattice -- an adaptive set needs the froxel
   nearest-probe path (already exists) or an indirection grid.

2. **The gather is O(sources x probes) with sources ~= all near-surface probes.**
   The near-SDF SOURCE rule + a denser lattice made N_sources (~2500) exceed
   gi_samples, which flipped the gather to per-frame random subsets -> flicker; the
   fix (full deterministic scan, gi_samples 4096) costs ~N^2 = ~7.6M source visits
   per update tick. Needed: stratified DETERMINISTIC sampling (per-probe hash of the
   source list, stable across frames -- no flicker, bounded cost), plus source
   CULLING by distance/cell so a probe only scans plausible contributors.

3. **Hero scan is a full list scan per probe** (cap 4096) for top-K selection --
   same N^2 shape; fold into the stratified structure.

Context: uniform-lattice cost showed up immediately when probe_spacing_scale 0.7
tripled the count; the flicker regression at the 1024-source threshold is the same
scaling wall. See gi_probe_gpu.c pass_classify/pass_gather.

## Literature survey verdict (2026-07-20, full report: ref/probe_placement_survey.md)

29 sources swept, 9 deep-read. **Adopt a hybrid of Unity-APV-style SDF-driven
placement + bake-time fix-up passes; geometric heuristics only — no ML earned a
place in placement.** Plan:

1. **Load-time placement (replaces the uniform lattice):** ternary brick
   hierarchy, brick at level k spans 3^k min-bricks, each brick = 4x4x4 probes at
   spacing brickSize/3. Keep a candidate brick iff `|SDF(center)| <= half the
   brick diagonal` (verified against Unity's ProbeVolumeSubdivide.compute,
   `sqrt(3)/(2N)`); finer bricks overwrite coarser; probes dedup by position hash
   (ternary nesting makes corner probes coincide exactly — do NOT change the
   3:1 / 4x4x4 ratios, they are the seam fix). fillEmptySpaces via the zone SDF
   ("inside playable space -> keep coarsest brick") so mid-air dynamics keep GI.
   Unity spends its whole GPU budget synthesizing the SDF — we already have ours
   baked, so placement collapses to one SDF query per candidate brick (CPU,
   load-time, days of work).

2. **Fix-up passes (the in-wall/leak killers — placement alone reproduces our
   current failures at higher density):**
   - Virtual offset: 26 fixed rays, tMax = 0.2 x local spacing; if >25% of
     closest hits are backfaces, push probe along best escape ray by
     minHitDist*1.05 + geometryBias. SDF-gradient version + a few mesh-soup
     verification rays (SDF alone blurs thin double-sided walls).
   - Backface-validity: per-probe backface-hit fraction — falls out of our
     EXISTING bake rays for free; thresholds 0.25 (dilation) / 0.05 (runtime).
   - Dilation: invalid probes get SH overwritten by 1/d^2-weighted average of
     VALID neighbors within ~1m. Do NOT dilate oct-depth data by averaging —
     re-trace depth at the dilated position (averaging can CREATE Chebyshev
     leaks).
   - Runtime: 8-bit corner-validity mask; zero invalid corners' trilinear
     weights + renormalize. Composes with (does not replace) the Chebyshev test.

3. **Runtime lookup:** world -> cell -> per-cell brick index (coarse bricks
   pre-splatted, no hierarchy walk) -> 8 probe IDs -> existing Chebyshev-weighted
   blend unchanged. SSBO indirection, GL 4.3 sufficient. Est. 1-2 weeks; this is
   the bulk of the work.

4. **Deferred options:** Vardis-2021 error-driven decimation (thin evidence,
   only AFTER clean-up — it preserves leaks in its reference field); IS-DDGI /
   ADGI update-ray scheduling when probe counts outgrow full per-frame updates
   (both placement-agnostic).

**Known residual + follow-ups (from the completeness critic):** thin walls with
valid probes on BOTH sides leak past validity masks — Unity's fix is authored
regions; candidate auto-fix is interior/exterior classification from the zone
SDF (unbudgeted). Worth reading before implementation: Activision Neural Light
Grid memo (their shipped answer to exactly that leak — learned per-probe
influence weights over a REGULAR grid, i.e. relevant to fix-ups not placement),
Wang et al. I3D 2019 (the placement-optimization paper everyone cites), and
medial-axis/SDF-ridge seeding as the open-air complement APV lacks (APV never
densifies high-gradient open air).

This survey addresses item 1 (placement). Items 2-3 (gather sampling cost)
remain as specced below — stratified deterministic sampling + source culling by
cell are unchanged by the placement choice, and the brick structure gives the
natural cell partition for the culling.

## Acceptance Criteria

Probe count driven by geometry (not AABB volume); gather cost bounded per probe
independent of total source count; no temporal flicker at any count.
