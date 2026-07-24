---
id: rpg-xpvu
status: open
deps: []
links: []
created: 2026-07-24T07:49:30Z
type: bug
priority: 2
assignee: KMD
---
# client_bake: chunked bake fails silently after whole-scene CPU voxelize (336M leaves)


## What happened
`--bake` of la_sprawl into `datasets/la_sprawl_hibake/` (launched 2026-07-23 14:16,
env: CHUNK=192 LMSCALE=0.15 SAMPLES=64 BOUNCES=2 ATLAS=4096 ARENA_MB=16384
LM_NEAR_DIM=128 LM_CHUNK_SPLIT=4 LM_DETAIL_DENSITY=1.0, NO CLIENT_BAKE_SDF) ran
~10.5 h at 100% CPU / 35 GB RSS, printed
`voxelize: 336865692 solid leaves, 3486602 with NO material (1.0% gap)`
then only `[client] bake FAILED`. No `[client_bake] chunk 0/...` line, no error
from lm_mesh_bake (the arena-too-small path prints; this one didn't).

## Analysis
- The voxelize banner is lm_svo_voxelize.c:185 — the **CPU** voxelizer. The bake
  binary predated the 17:43 rebuild (GPU voxelizer + later work), so the run used
  a stale demo_client with the slow CPU per-chunk/whole-scene voxelize.
- 336.9M solid leaves: node_count*8 child math and node-count-sized scratch
  (`area`, `vnormal`, visited masks) sit near/over UINT32 and multi-GB sizes.
  Machine had 236 GB free, so plain OOM is unlikely — suspect a silent
  `return false` on an alloc/count check between the voxelize and the first
  chunk print in lm_mesh_bake/lm_gpu_gather_chunked (several unlogged
  `return false` paths: client_bake.c:258,287,296 and gather-internal ones).

## To fix
1. Every `return false` on the bake path must fprintf the reason (arena rule
   already does this — extend to the malloc/gather/init failures).
2. Audit voxelize/octree size math for u32 overflow at >=2^28 leaves.
3. Consider refusing whole-scene CPU voxelize above a leaf budget with a clear
   "use the GPU voxelizer / chunked path" error instead of grinding for hours.

Related: [[rpg-bpiz]] (GPU voxelizer), [[rpg-th87]].
