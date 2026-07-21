<!-- Survey produced 2026-07-20 by a multi-agent literature workflow (29 sources
swept, 9 deep-read; per-agent notes + mirrored PDFs/Unity source in the session
scratchpad). Feeds ticket rpg-pjkb (probe placement + gather efficiency). -->

# Adaptive GI Probe Placement — Literature Survey Synthesis

## 1. Taxonomy of Approaches

| Family | Sources | 1-line verdict |
|---|---|---|
| **SDF/geometry-driven hierarchical subdivision** | Unity APV (ProbePlacement.cs + ProbeVolumeSubdivide.compute, both deep-reads); Godot SDFGI (titles-only) | **Best fit.** Shipped, default-on in Unity 6; the expensive part (building an SDF) is the part we already have. |
| **Bake-time probe fix-up (offset / validity / dilation)** | Unity APV Virtual Offset + Dilation + validity masks (deep-read); RTXGI Probe Relocation & Classification (titles-only) | **Directly answers the in-wall/leak problem**; small self-contained post-passes, orthogonal to any placement scheme. |
| **Illumination-aware decimation over irregular sets** | Vardis et al., EG 2021 poster (deep-read) | Sound greedy prune-by-error idea, but O(P²) retetrahedralizations, no timings published, and it *preserves* leaks baked into the dense set — a refinement pass, not a foundation. |
| **Tetrahedral irregular-set lookup** | Unity LightProbes/Cupisz GDC 2012 (deep-read of Unity docs) | Not a placement algorithm at all (blind parametric lattice), but the canonical runtime answer for irregular sets (4-tap barycentric) and a source of point-set conditioning rules. |
| **Per-frame update schedulers (mislabeled "adaptive")** | ADGI arXiv:2301.05125 (deep-read); IS-DDGI I3D 2023 (deep-read) | **Not placement.** Both keep a uniform lattice and ration *update rays*; both explicitly inherit DDGI leaking and cite the real placement papers as orthogonal. Park for a future update-cost ticket. |
| **Visibility-optimization placement** | Wang et al. 2019 "Fast Non-uniform Radiance Probe Placement and Tracing" (titles-only, cited as the placement lead by *both* ADGI and IS-DDGI) | Likely the strongest academic placement work, but **not deep-read in this survey — evidence gap, worth a follow-up read** before committing. |
| **Neural / ML** | Neural Light Grid 2024, WishGI 2025, Mobile-DDGI 2026 (all titles-only) | No deep-read evidence that ML beats the SDF half-diagonal heuristic; nothing in the read corpus needed learning. |

## 2. Ranked Candidates

### #1 — Unity APV-style SDF-driven brick subdivision (placement core)
**Sources:** both APV deep-reads (manual + Unity-Technologies/Graphics source: ProbePlacement.cs, ProbeVolumeSubdivide.compute).

- **Algorithm sketch:** Ternary brick hierarchy — brick at level *k* spans 3^k min-bricks, each brick = 4×4×4 probe lattice at spacing brickSize/3 (1/3/9/27 m ladder at defaults). Per streaming cell, for each level: keep a candidate brick **iff SDF(brickCenter) ≤ half the brick diagonal** (`voxelDetectionDistance = sqrt(3)/(2N)`), OR an authored fillEmptySpaces flag requests it. Finer bricks overwrite coarser in the index; probes deduped by position hash (ternary nesting makes corner probes coincide exactly across levels).
- **Inputs:** Unity spends its entire GPU budget (3-axis voxelization + jump-flood) *synthesizing* a per-cell SDF. **We have a baked SDF — the whole pipeline collapses to one SDF query per candidate brick per level**, top-down over a 3:1 octree. Optional authored min/max-subdiv volumes.
- **Evidence:** Qualitative, not quantitative — Unity publishes no benchmark numbers. But it is the shipped default GI system of Unity 6, and the count math is structural: 27× fewer probes per axis-tripling in empty space; only a shell around geometry gets fine bricks. Against our current ~1–3k uniform lattice, expect large open-volume savings or equivalently much finer near-surface density at equal count. Honest caveat: the criterion is distance-only — it densifies geometry-dense-but-lighting-flat regions and never densifies high-lighting-gradient open air (Unity documents this limitation).
- **Runtime lookup:** Two-level O(1) indirection, no kNN/tets: world→cell→per-cell brick index (coarse bricks pre-splatted over finer voxels, so no hierarchy walk)→4×4×4 atlas block→trilinear within the brick. Maps onto our existing "irregular set + indirection" froxel fallback. **Caveat:** Unity's single-hardware-trilinear-tap trick doesn't transfer to SH4+oct-depth probes — we use the index to recover 8 probe IDs and keep our existing Chebyshev-weighted 8-probe blend. The win is placement, not cheaper sampling. Brick-boundary seams need coincident corner probes (free from ternary nesting) or fine-brick clamping.
- **Effort in Ferrum:** Moderate. Placement itself is small (CPU, load-time, SDF queries — days). The real work is the sampling-shader refactor from one uniform grid to cell+brick indirection (SSBO, GL 4.3 is sufficient; Unity's "no OpenGL" restriction is about *their* compute path, nothing algorithmic). Estimate: placement 2–4 days, runtime indirection + seam handling 1–2 weeks.

### #2 — Virtual Offset + backface-validity + dilation + 8-bit neighbor mask (leak/in-wall killers)
**Sources:** APV fix-issues deep-reads (TraceVirtualOffset.urtshader, ProbeGIBaking.Invalidation.cs, Dilate.cs, ProbeVolume.hlsl). RTXGI relocation/classification (titles-only) is the same idea, independently validating it as industry standard.

- **Algorithm sketch:** (a) **Virtual Offset:** per probe, 26 fixed rays, tMax = 0.2×local probe spacing; if >25% of closest hits are backfaces, push the probe along the best escape ray by minHitDist×1.05 + 0.01 m. SDF variant: `if SDF(p) < eps: p += ∇SDF·(|SDF(p)|·1.05 + bias)` — but Unity's ray version handles non-watertight/thin double-sided walls that SDFs blur, so the faithful port is SDF walk + a few mesh-soup BVH verification rays. (b) **Validity:** per-probe backface-hit fraction — **falls out of our existing DDGI bake rays for free**; thresholds 0.25 (dilation) / 0.05 (runtime bit). (c) **Dilation:** invalid probes get SH overwritten by 1/d²-weighted average of neighbors within 1 m (restrict sources to valid probes — a strict improvement over Unity's sloppy version, trivial with an explicit probe list). (d) **Runtime mask:** 8-bit per-probe corner-validity mask; zero invalid corners' trilinear weights and renormalize/warp uvw — one extra R8 fetch.
- **Evidence:** Qualitative but strong: default-on in shipped Unity 6; before/after imagery showing black in-wall probes eliminated; community reporting concurs. Residual known failure: thin double-sided walls with valid probes on both sides — Unity's answer is rendering-layer regions + normal/view bias, not placement.
- **Runtime implication:** Near zero — one validity byte per probe, corner re-weighting composes with (does not replace) our Chebyshev test.
- **Effort:** Low. Validity is a counter in the existing ray pass; VO is ~26·N rays (~80k for 3k probes, negligible); dilation is a small load-time loop. **One open design question:** dilating oct-depth/visibility data is unvalidated — safer to re-trace depth at the dilated position or mark visibility "always occluded" (flagged in the deep-read). ~1 week total.

### #3 — Vardis 2021 illumination-driven decimation (optional refinement pass)
**Source:** EG 2021 poster deep-read + cgaueb/light_probe_placement prototype.

- **Algorithm sketch:** Start dense; greedily remove the probe whose removal least perturbs reconstructed lighting at scattered evaluation points (SMAPE in YCoCg, chrominance-weighted to preserve color bleeding), stop at target count or 3% error. Explicitly encoding-agnostic — works over our SH4 probes and any interpolation operator, including brick-trilinear.
- **Inputs:** dense baked probe set, evaluation points in playable space (**our zone SDF can generate these — the paper doesn't automate it**), and an interpolation operator.
- **Evidence:** Thin — 2-page poster, no timings, no probe-count/leak benchmarks; authors admit "high processing times." Naive cost is ~O(P²) full retetrahedralizations; the local-rebuild optimizations are TODO-comments, unimplemented.
- **Critical caveat:** the reference field is the *dense-set reconstruction*, so leaks in the dense bake are faithfully preserved. Must run **after** #2's clean-up, never instead of it.
- **Effort:** Moderate (1–2 weeks) if restricted to intra-brick candidate pruning over the APV output rather than free-form tetrahedral sets. Recommend deferring until #1+#2 numbers show pruning is still needed.

### #4 — Tetrahedral lookup constraints (Cupisz/Unity), as design insurance only
**Source:** Unity LightProbes docs deep-read. If we ever go fully irregular (e.g. Wang 2019-style), the reusable pieces are: 4-tap barycentric with tet-walk caching; stagger layers so no 4 points are near-coplanar; ≥2 vertical layers; near-equilateral spacing for conditioned weights; do our own epsilon dedup (Unity's claimed tolerance is undocumented). DDGI's 8-probe Chebyshev blend would need reworking to 4 probes — a real cost. Verdict: hold in reserve; the brick index in #1 makes it unnecessary.

## 3. Recommendation: Hybrid of #1 + #2, geometric heuristics only

**Adopt the APV recipe with our baked SDF substituted for Unity's voxelize+JFA stage:**

1. **Load-time placement:** ternary brick subdivision, keep brick iff |SDF(center)| ≤ half brick diagonal; fillEmptySpaces via the coarse zone SDF ("inside playable space → keep coarsest brick") so mid-air dynamic objects retain GI.
2. **Fix-up passes:** virtual offset (SDF-gradient push + mesh-BVH verification rays), backface-validity from the existing bake rays, source-restricted dilation, 8-bit corner-validity mask composed with Chebyshev at sample time.
3. **Runtime:** cell + brick-index indirection resolving to 8 probe IDs, existing DDGI blend unchanged otherwise; froxel fallback carries the validity mask.
4. Optionally, later: Vardis-style error-driven pruning of the APV output if counts are still too high.

**Why ML did not earn its place:** the license was "lightweight ML if it beats geometric heuristics." Nothing in the deep-read corpus provides evidence it does. The single scalar test `SDF ≤ half-diagonal` is what a production engine shipped as its default, and it exploits an asset we already own at essentially zero cost. The ML titles in the corpus (Neural Light Grid, WishGI, Mobile-DDGI) were titles-only — no verified evidence — and the two ML-adjacent deep-reads (ADGI's Metropolis chains, IS-DDGI's importance PDFs) solve update scheduling, not placement, and both concede they inherit DDGI leaking. The only credible non-heuristic contender, Wang et al. 2019 (visibility-optimized non-uniform placement, cited as *the* placement reference by two of the deep-read papers), was not deep-read — an honest gap, but not one worth blocking on given how well the SDF heuristic fits.

## 4. Pitfalls vs. Our Failure Modes

**In-wall probes**
- The APV subdivision criterion **does not cull buried probes** — Unity deliberately emits bricks touching geometry and relies on VO/validity/dilation (deep-read A, note in step 4). Porting placement without the fix-up passes reproduces our current failure at higher density. Optionally add an `SDF < -margin` cull, which Unity doesn't do.
- SDF-only virtual offset fails on thin double-sided and non-watertight geometry that the SDF blurs; keep the mesh-soup backface ray check (TraceVirtualOffset deep-read).
- Unity's dilation can pull from other *invalid* probes on iteration 1; restrict sources to valid ones.

**Leaks through walls**
- The residual leak Unity itself documents: probes on *both* sides of a thin wall are individually valid, so no mask fires. Unity's fix is authored rendering-layer regions (4 max); we lack that authoring — the deep-read suggests auto-classifying interior/exterior from the coarse zone SDF instead. Budget for this; validity masks alone won't close it.
- Vardis decimation preserves any leaking present in its reference field — order matters: clean first, prune second.
- Dilation of visibility/oct-depth data is unvalidated territory (Unity dilates SH + its own occlusion format); averaging neighbor depth maps can *create* Chebyshev leaks — re-trace depth at dilated positions.

**Flicker / seams at scale**
- Brick-resolution transitions cause visible seams; Unity mitigates with coincident corner probes (a property of the exact 3:1/4×4×4 ratios — **don't change those ratios casually**) plus per-frame sampling noise dithering (ProbeVolume.hlsl deep-read). Fine-brick clamping is the deterministic alternative.
- If probe counts later grow enough that per-frame full updates flicker or blow budget, that is the moment for IS-DDGI (allocation stage <0.1 ms at 4k probes, 2.1–2.5× total-pipeline speedup on measured scenes) or ADGI's frustum-culling + fast-catch-up hysteresis — both explicitly placement-agnostic and compatible with this plan. IS-DDGI itself warns of flicker in dimly-lit scenes from sparse change detection; irrelevant at our current 1–3k scale where full updates remain affordable.
- Tetrahedralization degenerates on coplanar/duplicate points (Unity docs + Vardis prototype both hit this); if any tet-based path is ever used, epsilon-dedup and layer staggering are mandatory, not optional.

**Sources referenced:** Unity APV deep-reads (manual + Graphics repo source, files mirrored under /tmp/claude-1000/-home-kmd-rpg/46a7a6a7-14f9-4468-a7dc-8fdc646c4acb/scratchpad/); Unity LightProbes/Tetrahedralize docs deep-read; Vardis et al. EG 2021 + cgaueb/light_probe_placement; ADGI arXiv:2301.05125 (scratchpad/adgi.pdf); IS-DDGI I3D 2023 (scratchpad/isddgi.pdf, isddgi.txt); titles-only (unverified): RTXGI relocation/classification docs, Wang et al. I3D 2019, Zhou et al. 2022 mask decomposition, Neural Light Grid 2024, WishGI 2025, Mobile-DDGI 2026, Godot SDFGI.

---

## Appendix: Completeness-Critic Findings (unresolved follow-ups)

Verification done. Findings:

- **(a) Missing family — medial-axis / free-space-skeleton placement:** absent from the taxonomy entirely. Given Ferrum already owns a baked SDF, placing probes at SDF local maxima/ridges is the natural complement to APV subdivision (which the survey itself concedes never densifies open-air regions), and it costs the same one-query-per-candidate machinery as candidate #1. Also swept-but-never-analyzed shipped *irregular placement* systems: CoD Infinite Warfare's light grid (Iwanicki/Sloan annotated slides) and Forza's "Adaptive Placement of Dynamic Lighting Probes" — both appear in the swept-titles list yet neither gets a row in the taxonomy, though both are production placement (not update-scheduling) systems.

- **(a) Unflagged evidence gap:** Silvennoinen & Lehtinen "Real-time GI by Precomputed Local Reconstruction from Sparse Radiance Probes" is swept-titles-only and is a genuine visibility-driven sparse-placement paper in the same class as Wang 2019 — but only Wang is honestly flagged as a follow-up gap; this one silently vanishes.

- **(b) Corpus-status contradiction:** the survey labels Neural Light Grid and Mobile-DDGI "titles-only (unverified)," but full sources are mirrored in the scratchpad — `memo.pdf`/`memo.txt` (the complete ATVI-TR-24-03 NLG technical memo), `nlg_slides.pdf`/`nlg_slides.txt` (full talk transcript), and `I3D2026_Mobile_DDGI.pdf` (7 MB full paper), with extracted figure PNGs (tbl-80..82, arch-63..65, hier-61) timestamped alongside. These were fetched and at least partially processed; calling them titles-only misstates the evidence base.

- **(b) APV technical claims verify:** spot-checks against the mirrored Unity source confirm the load-bearing numbers — `ProbeVolumeSubdivide.compute:261-265` is exactly the `sqrt(3)/(2N)`, keep-iff-`dist <= half-diagonal` test; `TraceVirtualOffset.urtshader` has 26 ray directions and `minDist * 1.05f + probe.geometryBias` (the survey's "+0.01 m" is the *default* geometryBias, not a constant — minor precision nit, not a contradiction). No deep-read contradictions found.

- **(c) ML verdict is part-evidence, part-vibes:** the conclusion (geometric heuristics for *placement*) is probably right — NLG deliberately keeps a uniform/cascaded grid and learns per-probe *influence weights*, not positions. But the stated justification ("titles-only — no verified evidence") is false per the bullet above, and the unread NLG memo is directly material to the survey's own #1 failure mode: its learned weighting functions are Activision's shipped answer to exactly the thin-wall both-sides-valid leak that the survey admits validity masks won't close (offering only "auto-classify interior/exterior from zone SDF" as an unbudgeted sketch). NLG should be evaluated against candidate #2/the leak section, not dismissed under placement. Not complete until that read is done or the verdict's basis is restated honestly.