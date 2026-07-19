---
id: rpg-51nf
status: closed
deps: []
links: []
created: 2026-07-19T07:44:03Z
type: task
priority: 1
assignee: KMD
parent: rpg-hjck
tags: [asset, format]
---
# Scene descriptor format + runtime loader

A standard on-disk scene/level descriptor that enumerates the level's meshes (fvma/glb/dmesh/obj), skeletons (fskel), lightmaps (.flm), manually-placed probes + probe-grid importance volumes, sdf/voxel chunks, materials, and physics colliders -- plus a runtime loader that resolves and hands them to the asset streamer. glTF is the top-level container for the visual scene graph (transforms/skeletons), with external mesh/asset refs allowed.

## Design

Prefer glTF (gltf_scene_load already exists) as the scene graph + a sidecar manifest for engine-specific lists. The manifest must reference, as first-class entries, the BAKER-GENERATED CHUNKED LIGHT DATA (already produced + consumed by hall_lit_dynamic.c; see rpg-nbp2 for the residency model):
- lightmap: base <lm>.flm and/or per-chunk <lm>_cNNN.flm + <lm>_manifest.bin (per-mesh layer/atlas-rect table).
- SDF / albedo-voxel chunks: <lm>_cNNN.sdf prefix (RGBA32F, rgb=albedo voxels / a=distance), sparse (empty regions have no chunk, e.g. great_hall c005 absent); each chunk carries its own world box.
- far-field: the baker's distant-reflector/sky contribution (lm_farfield) folded into the chunk data -- the descriptor just needs to mark that chunks are self-contained.
Plus: the probe spec (rpg-ft0g: optional manual probes / resolution-by-distance-LOD / AABB importance boxes), materials, and the physics collider set.
MUST capture the implicit invariant currently living only in hall_lit_dynamic.c: mesh enumeration order == lightmap bake order (today: sorted .dmesh names) and the per-mesh sh_layer/atlas-rect mapping. Loader emits a descriptor struct the render-world builder (rpg-i3wx) and server level-load (rpg-q1cp) both consume, and that the streamer (rpg-nbp2) resolves to chunk ids + priorities. No GL in the descriptor parse (server needs it headless).

## Acceptance Criteria

Descriptor parses headlessly into a struct listing every asset class with paths + transforms + the bake-order/sh-layer mapping; round-trips the great_hall scene (same mesh order, lightmap, probes, sdf) as hall_lit_dynamic.c assembles today; unit test loads a descriptor and asserts the asset lists + ordering.


## Notes

**2026-07-19T08:08:12Z**

Core descriptor + headless loader landed (TDD, 13/13 green incl. great_hall round-trip). Module src/scene/ + include/ferrum/scene/: scene_desc_t with ordered objects (bake order preserved), material table + name->index resolution, chunked light-data refs (lightmap/sdf prefixes, perchunk, manifest), and the probe spec (spacing/vspacing + AABB importance boxes + optional manual). JSON via the existing arena-based json_parse (no malloc, no GL); folded into libheadless.a so the server can parse levels headlessly. Test data: datasets/great_hall_export/great_hall.scene (generated from the exporter scene.json). REMAINING (add test-first when a consumer needs them with real data): explicit skeleton (fskel) refs and a physics collider-set section -- great_hall has neither, so per the no-backfilling rule they are deferred to rpg-q1cp (server colliders) / the skeletal path rather than added speculatively.

**2026-07-19T08:21:22Z**

Completed: added per-object fskel skeleton refs and the full physics collider-set section (box/sphere/capsule/halfspace/mesh, transform, object_ref, static flag) -- both test-first with real great_hall data (ground halfspace + static mesh colliders for the floor/piers). 15/15 tests green, -Wall -Wextra -Wpedantic clean, folded into libheadless.a. Every asset class in the descriptor is now enumerated + covered.
