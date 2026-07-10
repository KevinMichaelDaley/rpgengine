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

## Approach: Wang tiles built by quilting (single method)

One method for **all** materials: image-quilting-constructed **Wang tiles**
(Cohen 2003) laid out by a stateless coordinate hash. This keeps genuine exemplar
pixels and crisp structure everywhere (no statistical blur), and gives a provably
non-periodic, O(1)-sample-able, arbitrary-resolution field. A per-material config
only varies the patch/overlap size and edge-color count `C` (below).

**Construction (offline, per seed):**
- Choose `C` edge colors (start `C = 2`). For each edge color pick a sample strip
  from the exemplar; a tile with edge colors `(N,E,S,W)` is assembled from the
  four corresponding samples placed in a **diamond** (each sample rotated so it
  owns one edge of the tile), stitched along the diagonals with the **graphcut
  seam** (min-cut), so two tiles sharing an edge color have identical boundary
  pixels and abut seamlessly.
- A compact **stochastic set** needs, for every incoming `(west,north)` color
  pair, ≥2 candidate tiles (so the choice is free → aperiodic): Cohen's set is
  `2·C²` tiles (8 for `C=2`, 32 for `C=4`). More colors ⇒ less repetition.
- Save the tile set as a packed atlas + a small JSON manifest (tile → NESW colors).

**Runtime field sampling (`O(1)`, deterministic):**
- For output tile cell `(i,j)`: the west color must equal cell `(i−1,j)`'s east
  color and the north color equal `(i,j−1)`'s south — but to keep it stateless
  and hashable we use Cohen's trick: assign each **grid corner/edge** a color by
  hashing its integer coordinates (`edgeColor = hash(i,j,dir) mod C`), then the
  tile at `(i,j)` is uniquely determined by its four surrounding hashed edge
  colors. No scan, no state: any cell is reproducible from its coordinates alone.
- Sample within the cell by local `(u,v)`; the tile atlas lookup gives the pixel.

Storage: the tile atlas (small, e.g. 8–32 tiles × patch size).

## Baking the "massive fields"

The sampler exposes `field(u, v, seed) → RGB`. We **bake** each material's field
once into large images the Blender graph samples like a noise texture, so no
synthesis runs at shader time:

- Bake N large tiles (e.g. 4 × `4096²`) per material channel by evaluating the
  sampler over a big grid with a fixed `seed`. Because the sampler is aperiodic,
  the baked field shows no repeat within it; the node graph further randomises by
  offsetting/rotating its read (rpg-lbky), so even the bake period is hidden.
- Store baked fields at `assetsrc/materials/<mat>/fields/<mat>_<channel>_field_<k>.png`
  (checked in, they are inputs like the seeds). Keep the tiny Tier-A LUTs /
  Tier-B atlases alongside so fields can be regenerated/extended deterministically.

The baked field IS the "noise function": the material graph does an irregular
(triplanar / random-offset) lookup into it and never sees a seam or a repeat.

## Core primitive: minimum-error boundary cut

The algorithm the tile construction rests on, spelled out (Efros-Freeman DP path;
the graphcut variant swaps this step for min-cut on the diamond/corner overlaps):

```
place first patch at origin
for each subsequent patch position (raster order):
    pick a candidate patch from the exemplar (random, or best-SSD-in-overlap
        within tolerance — Efros-Freeman pick rule)
    E = (overlap(new) - overlap(placed))^2          # SSD error surface
    # vertical seam: min-cost path top->bottom
    Cumulative[i][j] = E[i][j] + min(Cum[i-1][j-1], Cum[i-1][j], Cum[i-1][j+1])
    backtrack least-cost column -> cut mask
    composite new patch over placed along the cut (feather 1-2 px)
```

Overlap `o ≈ patch/6`, patch size a per-material param (large enough to capture
the biggest feature: small for grain, larger for veins/joints).

## Module plan (`scripts/texsynth/`, extreme-modularity)

- `error_surface.py` — SSD overlap error (≤4 funcs).
- `min_cut.py` — DP minimum-error boundary path (vertical + horizontal).
- `graphcut.py` — min-cut/max-flow seam for 2-D/corner overlaps, via
  `scipy.sparse.csgraph.maximum_flow` (the diamond-tile diagonals).
- `quilt.py` — Efros-Freeman quilting driver (uses the two above).
- `wang_build.py` — construct the Wang tile atlas + manifest from an exemplar.
- `wang_sample.py` — hashed, stateless aperiodic tile-field sampler.
- `field_api.py` — unified `field(u,v,seed)` sampler; hashing utilities.
- `bake_fields.py` — CLI: read per-material config, bake + write field PNGs.
- `materials_synth.json` — per-material: patch/overlap size, `C` edge colors,
  channels, output field size/count.

Dependencies: **NumPy, SciPy, Pillow** in a **`uv` virtual environment** (this is
offline tooling — it does NOT run inside Blender, so we are free to use SciPy's
`csgraph` max-flow for the graphcut seam rather than vendoring one). Set up once:
`uv venv .venv && uv pip install numpy scipy pillow`. No engine coupling.

## Per-material config (edge colors / patch size)

All materials use the single quilting/Wang method; only the patch size and edge-
color count vary with how much structure the seed carries:

| Material | Patch | `C` | Note |
|---|---|---|---|
| limestone, sandstone, granite, terracotta | small | 2 | fine grain/speckle |
| plaster / fresco | medium | 2 | broad low-freq mottle |
| bronze, iron; all `rough` channels | small | 2 | micro-scratch/patina |
| marble | large | 2–3 | veins must stay crisp |
| flint | large | 3 | discrete nodules-in-mortar |
| brick | large | 3 | joint / running-bond structure |
| oak | medium (aniso) | 2 | directional grain |

More edge colors ⇒ less repetition but a bigger tile set; start at `C = 2` and
raise where a material shows repeats.

## Validation

- **No seam:** autocorrelation of a baked field shows no periodic peaks;
  visual tiling test at field edges.
- **Aperiodic:** two far-apart regions of a field are uncorrelated; Wang tile
  placement passes an edge-color-match assertion over a random grid.
- **Structure preserved:** baked field keeps the seed's crisp features (no blur);
  histogram ≈ seed within tolerance.
- **Deterministic:** `field(u,v,seed)` reproducible across runs (hash-seeded).
- **In-engine:** apply through rpg-lbky to column/arch/dome, confirm no repeat or
  seam across a large surface at grazing angles.

## Sources

- [Efros & Freeman, *Image Quilting for Texture Synthesis and Transfer*, SIGGRAPH 2001](https://people.eecs.berkeley.edu/~efros/research/quilting/quilting.pdf)
- [Kwatra et al., *Graphcut Textures: Image and Video Synthesis Using Graph Cuts*, SIGGRAPH 2003](http://gamma.cs.unc.edu/kwatra/publications/gc-final-lowres.pdf)
- [Cohen, Shade, Hiller, Deussen, *Wang Tiles for Image and Texture Generation*, SIGGRAPH 2003](https://www.cs.jhu.edu/~misha/Spring25/Readings/Cohen03.pdf)
- [Heitz & Neyret, *High-Performance By-Example Noise using a Histogram-Preserving Blending Operator*, HPG 2018](https://eheitzresearch.wordpress.com/738-2/) and [Deliot & Heitz, *Procedural Stochastic Textures by Tiling and Blending*, GPU Zen 2 (2019)](https://unity-grenoble.github.io/website/demo/2020/10/16/demo-histogram-preserving-blend-synthesis.html)
- [A survey of exemplar-based texture synthesis (Raad et al., 2017)](https://arxiv.org/pdf/1707.07184) — overview / taxonomy
