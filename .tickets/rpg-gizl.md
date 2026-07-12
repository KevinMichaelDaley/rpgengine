---
id: rpg-gizl
status: in_progress
deps: [rpg-ljxt]
links: []
created: 2026-07-10T06:51:38Z
type: task
priority: 1
assignee: KMD
parent: rpg-lb1q
tags: [arch, materials, texturesynthesis, procgen, python]
---
# Aperiodic texture-field synthesis from seed images

Turn the flat AI seed textures (assetsrc/materials/<mat>/, rpg-ljxt) into large,
non-repeating, sample-able texture FIELDS ("noise functions with the seed's
statistics") that the material node graph (rpg-lbky) samples irregularly. Design
is in ref/texture_field_synthesis.md.

## Summary

Offline Python tooling (scripts/texsynth/) that extends each ~1k seed to an
arbitrary-resolution, seamless, aperiodic field via example-based texture
synthesis, then bakes massive field PNGs sampled like noise. Two tiers, chosen
per material:

- Tier A (stochastic seeds: grain/speckle/mottle, all roughness): Heitz-Deliot
  histogram-preserving tiling-and-blending. Gaussianize + inverse LUT, blend 3
  randomly-offset virtual tiles with a variance-preserving operator. O(1),
  infinite, exact histogram, near-zero storage.
- Tier B (structured seeds: marble veins, flint, brick joints, oak grain):
  Wang tiles constructed by Efros-Freeman image quilting + graphcut seams
  (Cohen 2003), laid out by a stateless coordinate hash for a provably
  non-periodic O(1) infinite field that keeps hard structure crisp.

Shared primitive: minimum-error boundary cut (DP path; graphcut for corners).
Both tiers expose field(u,v,seed) -> RGB; bake_fields.py writes large field PNGs
to assetsrc/materials/<mat>/fields/ plus the tiny LUTs/atlases to regenerate.

## Deliverables

- scripts/texsynth/ modules per the design (error_surface, min_cut, graphcut,
  quilt, gaussianize, blend_field, wang_build, wang_sample, field_api,
  bake_fields) + materials_synth.json per-material config.
- Baked field PNGs for the seed materials, checked into assetsrc/.
- Validation: no periodic autocorrelation peak, aperiodicity/edge-match asserts,
  histogram match, deterministic reprops (see design doc).

## Notes

Pure offline tooling (NumPy/SciPy/Pillow), no engine coupling. TDD per repo
convention. Depends on rpg-ljxt (needs seeds); rpg-lbky depends on this (samples
the fields).


**2026-07-10T06:56:46Z**

SCOPE (KMD): Tier B ONLY — Wang tiles built by image-quilting + graphcut for ALL materials. Drop Tier A (Heitz-Deliot histogram blend) entirely; it blurs structure. Use SciPy (scipy.sparse.csgraph max-flow) for the graphcut seam in a uv venv (.venv) — offline tooling, does not run inside Blender. Design doc ref/texture_field_synthesis.md updated.

**2026-07-10T07:28:53Z**

Implemented in scripts/texsynth/ (uv .venv, TDD, 39 tests pass). PIVOT from square Wang tiles to irregular POLYGONAL patches (Kwatra graphcut textures) per KMD: square abutting tiles read as a periodic grid; polygonal graphcut seams remove it. Modules: hashing, error_surface, min_cut, graphcut (scipy max-flow), patches, patch_synth (core), field_api, bake_fields (+highpass for low-freq tonal blocks), materials_synth.json. Verified: limestone + marble 1280 fields are seamless, grid-free, veins crisp. Design doc updated. Perf: ~0.5s/patch, a 2048 field ~2min — optimize graphcut to overlap band later.
