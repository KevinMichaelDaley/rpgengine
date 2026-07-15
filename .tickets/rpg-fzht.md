---
id: rpg-fzht
status: in_progress
deps: []
links: []
created: 2026-07-15T20:49:13Z
type: task
priority: 1
assignee: KMD
parent: rpg-tjtp
---
# Chunked multi-resolution SDF/SVO scene regions for GPU lightmap baking


## Notes

**2026-07-15T20:50:43Z**

Split the lightmap bake into REGIONS/CHUNKS so arbitrarily large scenes can be
baked with bounded GPU memory, using detailed near geometry + downsampled distant
GI. Keep the design simple for a first pass.

1. GENERIC BAKE DRIVER: refactor hall_bake into a driver that takes an arbitrary
   scene-setup CALLBACK (meshes + materials + lights + bounds) instead of the
   hardcoded hall loading. hall becomes one caller of that driver.

2. CHUNKSET: partition the scene into fixed-size chunks. Per chunk:
   - Its own NEAR-field SDF (current coarse JFA resolution), OVERLAPPING the
     neighbouring chunks by a margin so rays can sample across chunk boundaries
     with no seam/transition.
   - Its own SVO (fine geometry + material for the gather's near hits).
   - Its own intersecting set of lightmaps/UVs (the luxels that fall in it).
   - Two DOWNSAMPLED far-field SDFs derived from the near SDF: a 128^3 MEDIUM-
     field and a 128^3 REALLY-FAR-field.

3. BAKE PIPELINE: for each lightmap bake, upload only the currently relevant
   NEAR + MEDIUM + FAR chunks to the GPU and gather that chunk's luxels against
   them (near SDF/SVO for close hits, medium/far SDFs for distant GI). Stream
   chunks in/out so total GPU residency stays bounded.

ARCHITECTURE: the SDF downsampling + chunking must live in the CORE RENDERER
module and be managed through the ASSET SYSTEM (chunks are assets, loaded/
streamed like textures/meshes) -- NOT baked-in to the offline tool. The SAME
chunk-load + downsample + stream path will be used AT RUNTIME in-game (coarse
RTGI for dynamic lights, contact shadows, streaming). The offline baker is just
one consumer of that shared renderer/asset machinery.

Builds on the single-SDF GPU gather (rpg-k4lk/yzmp done) and the persisted SDF
chunk format (rpg-iudw). Generalises one-SDF-per-bake into a streamed,
region-based, asset-managed multi-resolution field shared by bake + runtime.

First step: the scene-setup callback refactor of hall_bake.

**2026-07-15T21:00:59Z**

Step 1 DONE (commit b49e971a): generic bake driver lm_bake_driver (scene-setup callback -> optional-GPU bake -> .flm), headless CPU unit test, and hall_bake refactored into a caller. Next: chunkset partitioning (overlapping near SDFs + per-chunk SVO + downsampled 128^3 medium/far SDFs) as core-renderer assets.

**2026-07-15T21:13:15Z**

Steps 2-3 DONE (commits 8432f0f8, latest): chunk_grid (scene partition + overlap-margin intersection) and sdf_field (SDF grid sampling + resample/downsample for the 128^3 medium/far fields), both headless-tested. Remaining: per-chunk near-SDF/SVO build over chunk+margin, medium/far downsample orchestration per chunk, SDF chunk asset format (rpg-iudw FLM chunk), and the chunked bake loop (upload near+medium+far, gather chunk luxels, stream).

**2026-07-15T21:21:24Z**

FOUNDATION COMPLETE (commits b49e971a, 8432f0f8, d14c7569, 1b005eef, 06055261): lm_bake_driver (scene-setup callback refactor), chunk_grid (partition+overlap), sdf_field (sample/resample/downsample_region), sdf_field serialize (asset form / rpg-iudw). All headless-tested + in make test.

REMAINING = GPU integration phase (needs GPU test bandwidth):
1. Per-chunk near SVO + JFA SDF built over the chunk's OUTER (chunk+margin) bounds in lm_gpu_gather (currently one SDF over the whole svo_bounds).
2. Per-chunk medium/far: read back near SDF, sdf_field_downsample_region to 128^3 medium + far, upload as extra SSBOs/3D textures.
3. Multi-level gather shader: near SDF for close hits, medium then far downsampled fields past the transition (replaces the current single-SDF cone trace).
4. Chunked bake loop in lm_bake_driver/lm_mesh_bake: bucket luxels+meshes per chunk (chunk_grid_of_point/_overlaps_aabb), for each chunk build near+medium+far, gather its luxels, accumulate into the shared atlas, stream out.
5. Wire the SDF chunk into the .flm (rpg-iudw) for runtime reuse.

**2026-07-15T21:36:30Z**

GPU INTEGRATION - chunk loop DONE (commit 891dfcfa): lm_gpu_gather_run gained a region param (SDF over chunk box, SVO descent over full scene; region=NULL verified identical to before), and lm_gpu_gather_chunked partitions the scene, buckets luxels per chunk, and gathers each chunk's luxels against a per-chunk SDF over its outer box, scattering into the shared accum. Wired via config chunk_size/chunk_margin (HALL_CHUNK env). The chunked hall bake renders SMOOTH + SEAMLESS across chunk borders (overlap margin works); it is over-bright at small chunk sizes purely because rays leaving a chunk's SDF box escape to sky.

NEXT (correctness for small chunks) = medium/far far-field:
1. Build a scene-wide 'master' SDF once (whole-scene JFA).
2. far = downsample master -> 128^3 over the whole scene (shared by all chunks); per chunk medium = sdf_field_downsample_region(master) -> 128^3 over a ~3x-chunk region. Upload both as SSBO/3D-tex.
3. Extend the gather trace: near SDF for close, then continue into medium, then far, escaping to sky only when leaving the far field (replaces today's escape-at-near-boundary).
Then: shrink chunk_size for finer near SDFs at bounded memory + fold SDF chunk into the .flm (rpg-iudw).
