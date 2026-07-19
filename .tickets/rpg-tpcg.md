---
id: rpg-tpcg
status: open
deps: []
links: [rpg-iuiy]
created: 2026-07-19T03:49:00Z
type: feature
priority: 2
assignee: KMD
---
# Static batching in the asset system (merge same-material meshes)


## Summary
Static batching, done SYSTEMATICALLY by the asset system. The great hall is ~168
meshes but very little geometry, and MOST of it runs the SAME material (nearly
everything maps to the wall masonry group 0). That's a draw call per mesh for
almost no triangles -- draw-call-bound, not geometry-bound. Merge static meshes
that share a material into one batched mesh/draw so the forward pass issues a
handful of draws instead of ~168.

## Motivation (measured)
- Profiling (rpg-iuiy) showed the forward pass is the bottleneck, NOT the probe
  trace. A big chunk of forward cost is per-draw CPU + state overhead across the
  many tiny meshes. Static batching directly attacks that.

## Design
- At asset build / scene-load time (NOT per frame), group all STATIC renderables
  by (material, lightmap atlas layer / SH layer, any per-object uniforms that
  must stay separate) and concatenate their vertex/index buffers into one merged
  static_mesh per group, baking each source mesh's model transform into its verts.
- Keep dynamic objects (the sliding cubes, anything animated / re-transformed at
  runtime) OUT of the batch.
- Preserve per-object data the shader needs: lightmap uv1 / atlas rect and SH
  layer are already per-vertex or per-atlas, so they survive the merge; if any
  per-object uniform (e.g. u_sh_object, normal_scale) varies within a material
  group, either split the batch by it or fold it to a per-vertex attribute.
- Systematic: the asset/scene system produces the batches from the export
  manifest (material index per mesh is already known -- cf. gh_group), so no
  hand-authoring. The great-hall demo just consumes the batched meshes.
- Watch: batching enlarges bounding volumes -> coarser frustum/occlusion cull;
  keep spatial locality (batch per region/chunk) so culling stays useful.

## Acceptance
- Draw calls for the great hall drop from ~168 to O(materials * regions).
- Measurable forward-pass fps improvement, same image.
- Batching is produced by the asset system from the manifest, not per-scene code.
- Dynamic objects still render/transform correctly (excluded from batches).

## Status
DEFERRED -- do after the current GI-instrument/optimize + depth-prepass/overdraw
work (rpg-iuiy) lands.
