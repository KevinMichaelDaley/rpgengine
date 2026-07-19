---
id: rpg-npvc
status: open
deps: []
links: []
created: 2026-07-19T02:30:02Z
type: feature
priority: 2
assignee: KMD
---
# Skybox / sky-dome rendering integrated with GI (IBL, probes, lightmaps)


## Summary
Add a sky/skybox rendering system that is a first-class GI source, not a separate
flat backdrop. The sky is authored as very high resolution HAND-PAINTED skyboxes /
matte paintings (cubemap or, preferably, a sky DOME / hemisphere mesh) applied as
an EMISSIVE material, and it must drive every indirect-lighting path consistently:
split-sum IBL, the SDF-probe GI, and the offline lightmap bake all read the SAME
sky radiance so direct sky, reflections, and bounce agree.

## Design
- **Authoring:** support (a) a cubemap skybox and (b) a textured sky-dome /
  hemisphere mesh with an emissive (unlit) material sampled by direction; allow
  8K+ hand-painted mattes (streamed/mipmapped, possibly BC6H / RGBE for HDR).
- **Runtime sky pass:** render the sky in the existing skybox pass sampling the
  dome/cubemap by view ray.
- **IBL:** feed the same texture as the environment — prefilter to SH/SG for
  diffuse + a mip chain / SG lobes for specular — so split-sum IBL uses it instead
  of the current constant sky.
- **SDF-probe GI:** the cone-trace escape (ray that misses geometry) samples the
  sky texture by direction rather than a flat colour; temporal/EMA unchanged.
- **Lightmap bake:** the offline baker's sky term (currently LM_SKY_CONSTANT)
  reads the same dome/cubemap so baked indirect matches runtime; keep deterministic.
- **One sky definition** (texture + intensity + orientation) shared by pass, IBL,
  probes, and bake — avoids the sun/sky mismatch class of bugs (cf. the recent
  bake-vs-runtime sun fix).

## Acceptance
- A hand-painted high-res sky (cubemap or dome) renders in the skybox pass.
- The SAME sky drives split-sum IBL (specular+diffuse), SDF-probe escape radiance,
  and the lightmap bake sky term.
- Changing the sky texture/orientation changes reflections, probe bounce, and baked
  indirect consistently (no flat-colour fallback anywhere).
- Supports >=8K HDR mattes without stalling; the dome-mesh option works as emissive
  unlit geometry.
