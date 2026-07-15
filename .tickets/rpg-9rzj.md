---
id: rpg-9rzj
status: in_progress
deps: []
links: [rpg-1gj9]
created: 2026-07-13T07:49:25Z
type: feature
priority: 1
assignee: KMD
---
# Extend lightmap baker to triangle meshes (lightmap-UV atlas + per-texel luxelization)

The baker (lm_bake) currently luxelizes quad lm_surfaces only. Extend it to bake arbitrary TRIANGLE MESHES so real scenes (e.g. the romanesque hall) can bake their static/directional GI. Blender is used ONLY to generate each mesh's lightmap UVs via the standard 'Lightmap UVs' (lightmap pack) tool; the baker then offsets + atlases those per-object UV layouts into the scene lightmap atlas and generates luxels per covered texel. Each static mesh carries a lightmap_resolution controlling its texel density.

## Design

Core: (1) a mesh-lightmap-surface path in the baker that, for each triangle, rasterizes its lightmap-UV (uv1) footprint into the mesh's atlas region and, per covered texel, computes the world position + interpolated normal + albedo/emissive via barycentric coords -> a luxel. (2) Atlas packing: each object's [0,1] lightmap UV island layout (from Blender lightmap pack) becomes an atlas rect sized by its lightmap_resolution (reuse lm_atlas shelf packer). (3) SVO: rasterize the triangle meshes as occluders/reflectors (reuse lm_svo_stamp_mesh) with materials. (4) Wire into lm_bake so a scene of meshes+lights bakes to the SH atlas (reuse direct/indirect/farfield/solve). (5) static_mesh gets a lightmap_resolution field. (6) Blender step: export uv1 = lightmap-pack UVs alongside uv0. Diffuse-only, offline. TDD + modularity; no per-frame alloc.

## Acceptance Criteria

A triangle-mesh scene with per-object lightmap UVs + lightmap_resolution bakes correctly: luxels generated per covered texel with correct world pos/normal (barycentric), packed into the scene atlas, radiosity-solved, and the SH lightmap renders through the PBR shader. The romanesque hall bakes its directional sun into a lightmap that fills the walls/vaults with soft GI. Regression tests for triangle->luxel generation + atlas packing. Entirely in core baker; Blender only supplies lightmap UVs.

