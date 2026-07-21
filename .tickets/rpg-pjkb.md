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

## Acceptance Criteria

Probe count driven by geometry (not AABB volume); gather cost bounded per probe
independent of total source count; no temporal flicker at any count.
