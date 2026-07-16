---
id: rpg-0h4w
status: open
deps: []
links: []
created: 2026-07-16T03:37:51Z
type: bug
priority: 2
assignee: KMD
---
# Debug misc lightmap lighting artifacts

Segmentation/tiling artifacts visible in the streamed lightmap render (LM_STREAM on the zone/hall scenes). Pre-existing (present before froxel streaming), so not caused by residency.

Symptoms: brightness/tone discontinuities that track mesh-set seams — a stepped transition exactly where instanced meshes join, most visible on floors and vaults (columns mostly fine). Earlier investigation ruled OUT chart/atlas offsets as the cause (each instance has its own independent chart; large flat surfaces show luxel-density variation regardless).

Leads to investigate:
- Scene GEOMETRY, not the baker: instanced vault/floor meshes are one-sided / non-watertight at joins -> gather rays leak or self-occlude at seams. (User: 'this is a problem with the scene geometry not the baker.')
- Per-chunk SVO boundary: a mesh spanning a chunk boundary voxelizes into two per-chunk SVOs; verify far/medium SDF continuity across chunk edges so a luxel near the seam sees the same occupancy from either chunk.
- Luxel-density / lmres mismatch between adjacent mesh types (floor 192 vs vault 192 vs column) producing visible tone steps.
- Seed decorrelation already fixed (world-position hash); confirm no residual per-chunk RNG correlation at seams.

Repro: build/hall_bake_egl per-chunk bake -> render with tests/visual/hall_lit_dynamic (LM_STREAM=1). Compare seam regions on floor/vault.

