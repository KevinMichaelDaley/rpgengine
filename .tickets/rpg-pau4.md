---
id: rpg-pau4
status: closed
deps: []
links: [rpg-hw75]
created: 2026-07-18T18:50:36Z
type: feature
priority: 1
assignee: KMD
parent: rpg-fo9r
tags: [renderer, gi, probes, lightmap]
---
# Fold static lightmap into irradiance probes via lightmap-injected voxel radiance

The dynamic irradiance probes (rpg-fo9r) are in, but they only carry the DYNAMIC-light indirect term; the static indirect still comes from separately sampling the baked SH lightmap on surfaces. Fold the STATIC term into the probe SH coefficients too, so a probe holds the full indirect (static + dynamic) in one place.

Key insight: this reuses the EXISTING probe-update mechanism -- no new tracer. Inject the baked lightmap into the scene albedo voxelization as stored radiance, and have the update kernel's cone trace GATHER that lightmapped irradiance every time a cone hits a voxel. The probe then integrates exactly ONE bounce beyond whatever the offline lightmapper already computed -- static multi-bounce (from the bake) + one more dynamic-aware bounce, unified into the probe.

Same injected voxel radiance feeds the reflection-probe capture (rpg-hw75), so build it as shared infrastructure.

## Design

1. LIGHTMAP-INJECTED VOXELS: when building/updating the scene voxelization (the SVO/SDF albedo field the probe kernel already traces), store per-voxel a RADIANCE term = albedo * lightmapped_irradiance (sampled from the baked SH lightmap at that voxel's surface), with a boost factor so re-emission reads correctly as a bounce source. Voxels not covered by the lightmap fall back to albedo-only (dynamic lights still light them directly in the kernel). This is a one-time (or on-bake) fill for static geometry; keep it in the same voxel structure the dynamic path min-combines colliders into.

2. GATHER IN THE UPDATE KERNEL: in the per-probe cone-trace update (already parallel over probes), when a cone step hits a voxel, ADD that voxel's stored lightmapped radiance (attenuated by cone occlusion/AO and distance) into the probe's SH accumulation, alongside the existing dynamic-light contribution. No separate pass -- it rides the same cones. Net effect: probe SH = static bounced irradiance (from the injected lightmap) + dynamic-light one-bounce.

3. SHADER SIMPLIFICATION: with the static term in the probe SH, the forward+ material can source ALL indirect from the interpolated probe SH instead of separately evaluating the surface lightmap SH for indirect. Decide whether to (a) drop the direct lightmap-SH surface sample in favor of probes, or (b) keep the high-freq lightmap on surfaces and use probes only for the added bounce + dynamic -- avoid double-counting the static term either way. Likely keep the surface lightmap for detail and make the probe carry only the EXTRA bounce + dynamic; document the chosen split.

4. BOOST/ENERGY: expose the re-emission boost so the added bounce is visible but not runaway; validate energy against a reference (probe-off vs probe-on shouldn't blow out lit regions).

Modularity: extend the existing gi voxelization + probe-update modules in src/renderer/gi/; no demo-side logic. Guard compute behind the existing GL4.3 capability check with the CPU-job fallback. Respect 2-type / 4-function rules; TDD the host-side voxel-radiance fill + SH accumulation.

## Acceptance Criteria

- Static indirect appears in the probe SH: with all DYNAMIC lights off, probes still carry the baked static bounced irradiance (gathered from the lightmap-injected voxels), not zero.
- The static term is gathered by the SAME cone-trace update kernel (lightmap injected into voxel radiance), not a separate pass -- verified by the kernel reading voxel radiance during tracing.
- Probes integrate exactly one bounce beyond the offline lightmapper (no double-count with the surface lightmap sample; the static/dynamic split is documented and energy-validated -- lit regions don't blow out when probes are enabled).
- The injected voxel radiance is reusable by the reflection-probe capture (rpg-hw75).
- Boost factor exposed/tunable; clean under -Wall -Wextra -Wpedantic; TDD for the voxel-radiance fill + SH accumulation host logic (trace kernel validated by parity/visual). Demo: the hall reads correct static + dynamic indirect from probes alone, and the added bounce is visible vs probes-off.


## Notes

**2026-07-18T20:39:27Z**

SOLUTION (commits fe98802..43fea10c on main):

Static term folded into the probes by injecting the baked lightmap irradiance
into a coarse world volume (gi_static_volume, built from the .flm SH0..3 + mesh
uv1) that the probe cone-trace gathers at each SDF-occluded hit, sampled on the
probe-facing side so thin walls don't leak -- one bounce beyond the offline bake.

Stored as a SEPARATE static SH4 set per probe (24 floats/probe = dynamic SH4 +
static SH4) so the forward+ weights it per object: baked surfaces take a small
extra-bounce fraction (they already have the lightmap), dynamic objects take it
boosted (their only static ambience) -- gi_runtime_set_static_weights.

Sampling rewritten from froxel nearest-2-probe blend (which seamed across froxel
boundaries) to TRILINEAR interpolation over a regular probe grid
(gi_runtime_set_probe_grid); the froxel list is now just a cull. Probes densified
to a full interior grid so every cluster has coverage.

Leak-through-walls fixed with DDGI-style visibility: each probe stores an 8x8
octahedral depth map (mean + mean-squared distance) whose per-texel distribution
is estimated CHEAPLY from 3 SDF cone-traces (ray = mean; wide/narrow cones =
near-tail quantiles via probit, sigma from the quantile formula) instead of a
ray fan. Forward+ Chebyshev-weights each trilinear probe by the visibility of the
shading point; bilinear depth sampling + a normal bias remove the diamond and
probe-dot artifacts.
