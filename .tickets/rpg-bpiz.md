---
id: rpg-bpiz
status: closed
deps: []
links: []
created: 2026-07-23T09:22:49Z
type: task
priority: 1
assignee: KMD
---
# bake: GPU 3-axis voxel rasterizer replaces CPU dense stamping + SVO material subsample


(Description restored -- tk edit -d was silently dropping bodies.)

CLOSED via commits a022fe7c, a4dcab8f, f1c5f503: the client bake's CPU
voxelization (per-chunk octree triangle stamping + surface subsampling, plus
the dense conservative stamp for every near/medium/far SDF seed) is replaced
by GPU rasterization using the runtime volume slicer's mechanism
(gi_voxelize_draw.c): sliced render targets -- the channel volumes' layers
attach as MRT color targets one slice at a time and the ENTIRE mesh set is
drawn per slice with hardware clip planes (gl_ClipDistance slab); float ROP
blending accumulates (ADD; MIN transmission); no atomics, no shader-side
rasterization. Full bake voxel resolution is never capped: dense requests run
in z-slab windows; lm_gpu_voxelize_sample voxelizes cubic full-res tiles and
gathers at caller points; lm_gpu_chunk_svo_build compacts sparse leaf records
per tile (readback scales with octree LEAF COUNT) and descend-inserts them
through the public npc_svo pool API.

Result on la_sprawl (103 chunks): ~3.1 min/chunk CPU -> ~2 s/chunk GPU
(~90x); whole draft bake 5.5h+ -> 211 s; material gap 14-16% -> ~0.0%.
GPU/CPU solid-leaf parity EXACT on the cube fixture (56 = 56). Tests:
tests/lightmap/lm_gpu_voxelize_tests.c (EGL battery, 58 checks).
