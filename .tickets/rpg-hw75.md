---
id: rpg-hw75
status: in_progress
deps: [rpg-pau4]
links: [rpg-pau4]
created: 2026-07-18T18:45:50Z
type: feature
priority: 2
assignee: KMD
parent: rpg-fo9r
tags: [renderer, gi, reflections, probes]
---
# Cubemap specular reflection probes (SVO-raytraced, Gaussian-lobe compressed)

Add a glossy/specular reflection term to the GI probe system, co-located with the existing dynamic irradiance probes (rpg-fo9r) so both share one probe placement/accel structure and a single sampling lookup in the forward+ material.

Where the irradiance probes store SH9 diffuse irradiance, a reflection probe stores a COMPACT specular basis: a small set of Gaussian lobes (direction + sharpness + RGB weight, i.e. a spherical-Gaussian mixture) fit to the probe's radiance so rough specular reflections can be reconstructed cheaply per-fragment without a full cubemap fetch. Sharing the probe location keeps sampling efficient: one trilinear probe lookup yields both the diffuse SH and the specular SG lobes.

Motivation: rough/semi-gloss surfaces (marble dais, polished stone floor, timber) currently only get diffuse indirect; they need environment specular that responds to the dynamic scene.

## Design

Pipeline (mirrors and reuses the irradiance-probe pipeline where possible):

1. CAPTURE (dynamic, throttled): for each reflection probe, raytrace the scene directly through the SVO/SDF (the same combined baked-SDF + analytic dynamic-collider field the irradiance probes cone-trace) to gather incident radiance over the sphere. Reuse the probe update kernel's tracing; here we want a low-res radiance CUBEMAP (e.g. 16-32 px/face) per probe rather than an SH accumulation. Round-robin a fraction of probes per frame.

2. COMPRESS TO BASIS: fit the captured cubemap to a small spherical-Gaussian (Gaussian-lobe) mixture -- N lobes (start N=3-5), each {axis, sharpness lambda, RGB amplitude}. This is the stored, cache-friendly representation; the raw cubemap is transient scratch. Optionally: a fully STATIC bake path that captures + compresses once offline (like the lightmap) for probes in static regions, with the dynamic path only refreshing probes near moving lights/geometry -- "compressed down to the basis statically" per the request.

3. STORE: extend the probe buffer (SSBO / texture array) with the SG lobe set alongside the SH9. Keep it small so trilinear interpolation of 8 corner probes stays cheap -- interpolate lobe amplitudes (and possibly axes) or pick the nearest probe for specular if interpolating lobes is too lossy.

4. SAMPLE (forward+ material): for a fragment with roughness r and view/reflect vector R, evaluate the interpolated SG mixture along R with a lobe sharpness derived from r (rough -> wide lobe -> the SG eval is naturally a prefiltered reflection). Add as the specular indirect term on top of the diffuse probe SH + baked lightmap + direct lights. Very smooth mirrors are out of scope (SG is for rough/glossy); clamp roughness floor.

Parallelism: capture + fit are per-probe, embarrassingly parallel (compute dispatch on GL4.3, else job-system parallel_for over probes). Fit is a small per-probe optimization (few lobes, few iterations) -- keep it bounded.

Modularity: build in the renderer GI modules (src/renderer/gi/), demo only invokes -- same split as the SDF-probe GI. New public headers respect the 2-type rule; kernels/fit helpers stay static in .c files under the 4-function rule. Guard any GL4.3 compute behind the existing capability check with a CPU-job fallback.

## Acceptance Criteria

- Rough/glossy static surfaces (marble dais, stone floor) show an environment specular highlight/reflection sampled from the co-located probes, reconstructed from the stored Gaussian-lobe (SG) basis -- NOT a per-fragment cubemap fetch.
- Specular and diffuse indirect come from ONE probe lookup (shared placement + accel with rpg-fo9r's irradiance probes).
- Dynamic path: reflections near moving lights/geometry update over frames (throttled) with no rebake; a static bake path compresses static-region probes to the SG basis once.
- Roughness drives lobe width so rougher = blurrier reflection; combines additively and correctly with the baked lightmap, diffuse probe SH, and direct lights.
- Probe storage stays compact enough for trilinear interpolation of the SG set to remain cheap in the forward+ shader.
- Clean under -Wall -Wextra -Wpedantic; TDD for host-side probe-buffer/SG-fit/serialization logic (trace + fit kernel validated by parity/visual). Demo: the hall floor/dais reflect the firelight + window light, updating as the fire flickers.


## Notes

**2026-07-18T20:58:42Z**

FIRST INCREMENT (committed): single moment-fit SG lobe/probe.

- Lobe fit in the existing probe trace: luminance-weighted mean radiance dir =
  axis; vMF sharpness from the mean resultant length; total colour = amplitude.
  8 floats/probe in a new SSBO/TBO beside the SH + DDGI depth.
- Forward+: trilinear blend of the 8 grid probes' lobe along the reflection R,
  roughness lowers the effective sharpness, Fresnel-weighted, VISIBILITY-weighted
  (probe_vis + normal bias) so probes behind the roof/walls don't leak their
  reflection, AO-modulated. gi_runtime_set_spec_gain / SPEC_GAIN (subtle default).

Also this pass:
- Depth-probe leak fix: cone radius shrinks with the ray distance (tight creases
  like roof edges use tight cones) + lowered base cone slopes so they don't
  subsample the SDF; sharpens the Chebyshev visibility everywhere.
- Sky-openness AO now applied MULTIPLICATIVELY to the static + probe indirect
  (u_gi_ao_mult, gi_runtime_set_sky_ao's new arg), not just additive sky ambient.

TODO: multi-lobe residual fit; interpolate lobe params vs evaluate-then-blend;
static bake path; TDD for host SG-fit/serialize.
