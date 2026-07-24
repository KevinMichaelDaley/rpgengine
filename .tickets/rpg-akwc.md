---
id: rpg-akwc
status: closed
deps: []
links: []
created: 2026-07-24T09:49:21Z
type: feature
priority: 1
assignee: KMD
---
# Sparse cubemap reflection probes: baked atlas + split-sum sampling, SG probes fill the gaps


A separate, MUCH sparser set of "cubemap probes" for reflections,
alongside (not replacing) the dense SH/SG irradiance probes.

## Bake side
- Render each probe roughly like the cubemapped shadows
  (src/renderer/shadow_cube.c pattern: one pass over the scene into the
  cube faces) but rasterizing the ENTIRE scene, and the render must also
  carry an IRRADIANCE term, not just raw radiance.
- Rasterized SINGLE-PASS at bake time, then PROGRESSIVELY FILTERED
  (roughness-targeted filter chain over the faces), and stored in an
  ATLAS.
- Cached by the baker as objects (the lightmap/probe bake chain in
  src/renderer/world/client_bake.c + src/lightmap/ owns them like it
  owns .flm/.probesh artifacts).
- The atlas ALPHA channel is the baked SPECULAR OCCLUSION derived from
  the SDF (the per-chunk .sdf fields the bake already emits).

## Shade side (fragment)
- Sample with SPLIT-SUM (prefiltered radiance x BRDF LUT), with a
  SPECULAR CAVITY term derived from the surface AO and ROUGHNESS maps
  modulating the ridges.
- The SH/gaussian-lobe probes are ALREADY sampled through another
  version of this same path (gi_probe_specular / u_probe_sg SG lobes in
  src/renderer/pbr_shader.c, rpg-hw75). Do NOT build a second pipeline
  for them: modulate the existing SG specular by (a) the probe's own AO
  value (the probes already carry one) and (b) the surface AO and
  roughness values. With that modulation in place they automatically
  fill the sampling gap wherever the sparse cubemap set has no nearby
  probe.

## Notes
- Probe placement: reuse the probe_bake density machinery but at a far
  sparser spacing (own knob in render.json alongside gi_brick_*).
- Runtime binding: atlas + per-probe meta (position, extents, mip
  count, atlas rect) analogous to the brick/probe meta buffers.
- Related: [[rpg-hw75]] (SG specular lobes), [[rpg-fo9r]] (SDF
  cone-traced irradiance probes), [[rpg-lmph]] (merge the duplicated
  probe gathers -- do that first or land this on the merged path),
  [[rpg-9u96]] (cube shadow render loop this borrows from),
  [[rpg-th87]] (probe bake iteration bug -- avoid the same trap in the
  cubemap bake pass).

## Acceptance
1. Baker emits the cubemap-probe atlas (+ meta) for gh_dyn and
   la_sprawl; re-bake is deterministic.
2. Split-sum sampling visible on glossy materials (mini-mall plate
   glass / hall stone) with specular occlusion in the alpha actually
   darkening cavities; no double-counting against the SG specular.
3. SG-probe specular modulated by probe AO x surface AO/roughness;
   regions without a cubemap probe show no visible seam where the SG
   fallback takes over.
4. Perf: fragment cost within the existing GI budget knobs
   (render.json gated, default on for both test scenes).

## Implemented 2026-07-24 (9539885f)
Modules in src/renderer/gi/refl/ (octa map, atlas layout, progressive
filter, SDF placement + occlusion cone, RFP1 file, GL bake, GPU bind);
shader split-sum + cavity + SG cross-fade in pbr_shader.c; knobs
refl_* in render.json. Bake rides --bake-probes (CLIENT_BAKE_REFL=0
skips, REFL_SPACING overrides). Known v1 limits, follow-up material:
- probe selection is distance-weighted only (no visibility weighting);
  an interior fragment near an exterior probe can sample through walls.
  Mitigated by SDF sun-visibility in the bake + near-geometry placement.
- bake lighting is tint x (sun x probe-sun-vis + hemispherical ambient)
  + emissive: no textures, no per-fragment shadows, no probe-SH bounce.
- fragment probe pick is an O(count<=128) loop; fine at 25-64 probes,
  needs a froxel/brick index if counts grow.

## v2 (same day): full-forward bake + visibility
- The cubemap bake now renders every face through the REAL forward
  pipeline (refl_bake_params_t.render_fn -> render_world_update with the
  bake FBO as target): shadows, lightmaps, GI, glass -- true mirror
  source data. Gamma inverted at readback; 2x supersampled faces.
- Probe selection is visibility-weighted: per-probe octahedral RG
  (mean, mean^2) radial-depth atlas baked from the face depth buffers,
  Chebyshev-tested per fragment (u_refl_depth, unit 42, RFP2 format).
  Kills through-wall probe leaks; the SG fallback covers rejected areas.
