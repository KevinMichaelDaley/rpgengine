# Aperiodic texture-field synthesis from AI seed images

Design for turning the small, flat AI **seed textures** (`assetsrc/materials/<mat>/`,
ticket rpg-ljxt) into **arbitrarily large, non-repeating, sample-able texture
fields** — "noise functions with the statistics of the seed" — that the Blender
material node graph (rpg-lbky) samples irregularly. Part of epic **rpg-lb1q**.

## Problem

The seeds are ~1k px squares. We need to sample surface detail across large
architectural meshes with **no visible tiling seam and no periodic repeat**, at
effectively unbounded resolution, from these fixed exemplars. We are not using
the seeds directly; we resample them irregularly. The requirement (KMD): extract
**single-pixel-accurate aperiodic tilings** — "stamp" patches that combine at
their edges to extend an exemplar to arbitrary size — then **pre-generate massive
image fields we can sample like a noise function**.

This is the classic **example-based texture synthesis** problem. The literature
gives us three interlocking tools; the design uses all three, picking per
material.

## Literature (the three tools we build on)

1. **Image Quilting — Efros & Freeman, SIGGRAPH 2001.** Lay down overlapping
   square patches cut from the exemplar; in each overlap region compute the
   **minimum-error boundary cut** (a min-cost path through the SSD error surface,
   by dynamic programming) and composite along it. This is the *pixel-level
   stamp-and-stitch primitive* KMD is describing. Arbitrary output size, cheap,
   preserves local structure.
2. **Graphcut Textures — Kwatra et al., SIGGRAPH 2003.** Generalises the boundary
   cut from a 1-D DP path to an **optimal 2-D seam via min-cut/max-flow**. Needed
   when patches overlap on more than one side (corners, the diamond tiles below),
   where a single DP path can't express the seam. We use it for tile construction.
3. **Wang Tiles for Image & Texture Generation — Cohen, Shade, Hiller, Deussen,
   SIGGRAPH 2003.** Precompute a **small set of square tiles whose edges carry
   matching "colors"**; any edge-compatible arrangement is seamless, and a
   **stochastic scanline placement is provably non-periodic**. Crucially, Cohen
   *constructs the tiles themselves with Efros-Freeman quilting* — so (1)+(3)
   compose exactly into what we want: a compact tile set + an O(1) hash that
   expands to an infinite aperiodic field.
4. *(considered and rejected)* **Procedural Stochastic Textures by Tiling &
   Blending — Heitz & Neyret / Deliot & Heitz.** Gaussianize + variance-
   preserving blend of offset copies. Infinite and cheap, but it **blurs
   structure and washes out the seed's character** — rejected by KMD ("crap").
   We do NOT use it. Every material goes through the quilting/Wang path below,
   which keeps real pixels and real edges.

## Approach: irregular polygonal patches via graphcut (single method)

One method for **all** materials: **graphcut textures** (Kwatra 2003). Overlapping
exemplar patches are placed across the output and merged along **graphcut min-cut
seams**, so each patch ends up owning an **irregular polygonal region** — there is
no axis-aligned square grid for the eye to lock onto. This keeps genuine exemplar
pixels and crisp structure everywhere (no statistical blur), is aperiodic, and
synthesises to any size.

> **Why not square Wang tiles?** They were the first design (and are implemented
> in git history), but square abutting tiles read as a periodic grid: the eye
> groups the axis-aligned tile edges into rows/columns, and low-frequency mean
> differences between tiles pool into visible blocks. KMD's call: *make the tiles
> polygonal.* Irregular graphcut patches remove the grid entirely.

**Synthesis (offline, per seed):**
- Place patches on a coarse grid with a large overlap (`overlap ≈ patch/2`), in
  raster order. Each patch's source location in the exemplar is chosen by a
  **deterministic hash** of its cell coordinates → reproducible and aperiodic.
- Merge each new patch against the already-committed pixels over their overlap
  with the **graphcut seam** (`min-cut/max-flow`, `scipy.sparse.csgraph`): pixels
  only the new patch covers are forced to the new patch, the committed frontier
  is anchored to the old, and the min-cut carves the irregular boundary between.
- Because the seam wanders freely across a wide overlap and every patch draws
  from a different hashed source, no two boundaries align — the result is a field
  of interlocking polygons with no visible periodicity.

**Low-frequency flattening (structured seeds):** graphcut hides high-frequency
seams perfectly but leaves patches at slightly different overall brightness, which
reads as faint tonal blocks on seeds with broad low-frequency variation (marble
cloud, brick colour drift). A **high-pass** preprocess (`highpass_sigma`) removes
variation coarser than ~a patch and re-centres on the global mean, so only the
crisp detail — which the seams hide — is tiled. Off (`0`) for tonally-flat seeds
(limestone, granite).

## Paired diffuse + roughness (correlated channels)

Albedo and roughness are synthesised **together**, not independently, so the
roughness corresponds spatially to the diffuse (`paired_synth.py`). For each
placed diffuse patch we search — sliding-window normalised cross-correlation on
low-res copies of every roughness seed variant, then a full-res
`match_template` refinement around the coarse peak — for the roughness window
whose structure best matches the diffuse patch (either polarity: bright-polished
and dark-rough both count). That roughness patch is placed at the same field
location under the **same seam mask** as the diffuse patch, so the two fields
share geometry exactly. The coarse/low-res search keeps it fast; the FFT
refinement makes the alignment pixel-exact even on fine detail.

**Roughness histogram normalisation:** each chosen roughness tile is matched to
the material's overall mean and spread (`_normalize_tile`), removing its
low-frequency level/contrast — the cause of roughness tonal blocks — while
keeping the high-frequency detail that carries the correspondence. So the
roughness field has a uniform overall level (the level itself is set later by
the material's roughness parameter) with HF detail that tracks the diffuse.

## Baking the "massive fields"

We **bake** each material's field once into large images the Blender graph samples
like a noise texture, so no synthesis runs at shader time:

- Bake N large fields (e.g. `2048²`) per material channel with a fixed `seed`.
  The field is aperiodic within itself; the node graph further randomises by
  offsetting/rotating its read (rpg-lbky), so even the bake extent is hidden.
- Store baked fields at `assetsrc/materials/<mat>/fields/<mat>_<channel>_field_<k>.png`.
  The synthesis is deterministic in `seed`, so fields regenerate exactly from the
  seeds + `materials_synth.json` and need not be tracked if space is a concern.

The baked field IS the "noise function": the material graph does an irregular
(triplanar / random-offset) lookup into it and never sees a seam or a repeat.

## Core primitive: the graphcut seam

Each patch merge is a min-cut/max-flow labelling of the overlap: adjacent pixels
`s, t` are cut at cost `M(s,t) = ||A(s)-B(s)|| + ||A(t)-B(t)||` (Kwatra), pixels
pinned to the old/new patch are `∞`-bound to the source/sink terminals, and the
min-cut is the least-visible boundary. The `O(1)` **minimum-error boundary cut**
(Efros-Freeman DP path, `min_cut.py`) is retained as the cheaper 1-D primitive and
for cross-checking, but the 2-D graphcut is what carves the polygons.

## Module plan (`scripts/texsynth/`, extreme-modularity)

- `hashing.py` — SplitMix64 integer hash → deterministic aperiodic patch sources.
- `error_surface.py` — SSD overlap error.
- `min_cut.py` — DP minimum-error boundary path (retained primitive).
- `graphcut.py` — min-cut/max-flow seam via `scipy.sparse.csgraph.maximum_flow`.
- `patches.py` — random exemplar region sampling.
- `patch_synth.py` — graphcut-textures polygonal patch placement (the core).
- `paired_synth.py` — joint diffuse+roughness synthesis with correlated rough
  tile selection + per-tile roughness histogram normalisation.
- `field_api.py` — `synth_field(exemplar, w, h, patch, overlap, seed)`.
- `bake_fields.py` — `highpass` + CLI: read config, bake + write field PNGs.
- `materials_synth.json` — per-material: patch/overlap, highpass_sigma, channels,
  field size/count.

All modules have `tests/` alongside (run: `.venv/bin/python -m pytest
scripts/texsynth/tests`).

Dependencies: **NumPy, SciPy, Pillow** in a **`uv` virtual environment** (this is
offline tooling — it does NOT run inside Blender, so we are free to use SciPy's
`csgraph` max-flow for the graphcut seam rather than vendoring one). Set up once:
`uv venv .venv && uv pip install numpy scipy pillow`. No engine coupling.

## Per-material config (patch / overlap / high-pass)

All materials use the single graphcut-patch method; the config varies patch size
with the seed's feature scale and turns on high-pass for tonally-varying seeds
(`materials_synth.json`). `overlap = patch/2` throughout:

| Material | Patch | High-pass | Note |
|---|---|---|---|
| limestone, sandstone, terracotta | ~176 | off | fine grain/speckle, tonally flat |
| granite | ~144 | off | dense crystalline speckle |
| bronze, iron; `rough` channels | ~176 | 48 / off | micro-scratch/patina |
| plaster / fresco | ~224 | 64 | broad low-freq mottle |
| marble | ~256 | 56 | veins must stay crisp |
| flint | ~240 | 64 | discrete nodules-in-mortar |
| brick | ~256 | 72 | joint / running-bond drift |
| oak | ~208 | 48 | directional grain |

Bigger patches capture bigger features but leave fewer, larger polygons; raise
high-pass where a material shows tonal blocks.

## Validation

- **No grid:** the field has no axis-aligned periodicity (visual + autocorrelation
  with no peaks at the patch spacing).
- **Values are real pixels:** every output pixel is a verbatim exemplar pixel
  (unit test: field values ⊆ exemplar values) — no blend/blur.
- **Full coverage:** a constant exemplar fills every pixel (no holes).
- **Structure preserved:** baked field keeps the seed's crisp features; high-pass
  collapses low-frequency variation while local detail survives (unit-tested).
- **Deterministic:** `synth_field(..., seed)` reproducible across runs (hashed).
- **In-engine:** apply through rpg-lbky to column/arch/dome, confirm no repeat or
  seam across a large surface at grazing angles.

## Sources

- [Efros & Freeman, *Image Quilting for Texture Synthesis and Transfer*, SIGGRAPH 2001](https://people.eecs.berkeley.edu/~efros/research/quilting/quilting.pdf)
- [Kwatra et al., *Graphcut Textures: Image and Video Synthesis Using Graph Cuts*, SIGGRAPH 2003](http://gamma.cs.unc.edu/kwatra/publications/gc-final-lowres.pdf)
- [Cohen, Shade, Hiller, Deussen, *Wang Tiles for Image and Texture Generation*, SIGGRAPH 2003](https://www.cs.jhu.edu/~misha/Spring25/Readings/Cohen03.pdf)
- [Heitz & Neyret, *High-Performance By-Example Noise using a Histogram-Preserving Blending Operator*, HPG 2018](https://eheitzresearch.wordpress.com/738-2/) and [Deliot & Heitz, *Procedural Stochastic Textures by Tiling and Blending*, GPU Zen 2 (2019)](https://unity-grenoble.github.io/website/demo/2020/10/16/demo-histogram-preserving-blend-synthesis.html)
- [A survey of exemplar-based texture synthesis (Raad et al., 2017)](https://arxiv.org/pdf/1707.07184) — overview / taxonomy
