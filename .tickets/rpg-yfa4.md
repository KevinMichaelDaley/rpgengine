---
id: rpg-yfa4
status: open
deps: []
links: []
created: 2026-07-15T23:21:18Z
type: task
priority: 1
assignee: KMD
parent: rpg-fzht
---
# Per-chunk lightmap atlases for the massive zone


## Notes

**2026-07-15T23:21:32Z**

GOAL: one lightmap atlas PER CHUNK (not one global 4096 atlas spread over the whole zone -> too low res). Requirements from user: real col/vault meshes (done: gen_zone instances hall_demo_col_0/vault_0_0, 1345 meshes @24 bays), smaller chunks, higher res, thousands of meshes (renderer done).

DESIGN:
1. BAKE (separate luxel-scene from geo-scene): lm_mesh_bake currently uses scene->meshes for BOTH luxelize/atlas AND SVO/gather geometry. Add an optional geometry scene (full scene) distinct from the luxelize scene (one chunk's meshes). Then a per-chunk bake loop: partition meshes by centroid into a chunk grid; for each chunk bake ONLY its meshes into a fresh atlas but gather against the FULL scene (per-chunk SVO from lm_chunk_svo_build over chunk box+margin already builds from full scene + shared far field). Emit one flm per chunk: zone_cXXX.flm. Reuse lm_mesh_luxelize/lm_atlas/lm_gpu_gather_run/lm_lightmap_save.
2. RENDER (multi-pass): hall_lit_dynamic loads N per-chunk flms (N x 9 SH atlases). render_forward reads cfg.sh_tex at RENDER time, so group meshes by chunk and render one pass per chunk with fwd.cfg.sh_tex set to that chunk's 9 textures + that chunk's meshes (don't clear depth between passes). Each mesh's uv1 remapped into its chunk atlas rect.
3. Optionally fold each chunk's near/med SDF into its flm (rpg-iudw, sdf_field serialize exists) for streaming.

STATUS: not started. Prereqs done: per-chunk SVO (rpg-fzht), 3-level SDF, renderer scales to thousands of meshes + debug-asset loader, real-mesh instancing generator.

**2026-07-15T23:26:14Z**

RENDER DESIGN CORRECTION (per user): do NOT render per-chunk passes -- geometry (and meshes) may span chunk boundaries. Instead:
- Store the per-chunk lightmaps as a UNIFORM TEXTURE ARRAY (one layer per chunk, all layers same size = the 'uniform array'/page table). Each luxel/vertex uv1 carries its chunk's layer index (or derive chunk from world pos in the shader).
- Use a FROXEL-STYLE (clustered, like the existing forward+ light cluster grid) buffer to determine which chunk pages are visible from the current view, so only visible chunk lightmap pages are resident/bound (streaming). The froxel buffer maps view clusters -> needed chunk page ids.
- Fragment shader: world pos -> chunk id -> page-table lookup -> sample that array layer at the mesh's uv1. One draw of the whole scene; correct atlas chosen per-fragment, so meshes spanning chunks are fine.
This replaces the earlier (wrong) 'one render pass per chunk' idea.

**2026-07-15T23:34:59Z**

PROGRESS 2026-07-15: BAKE HALF DONE (committed+pushed).
- lm_bake_config.geo_scene added; lm_mesh_bake gathers against geo (full scene) while luxelizing scene->meshes (one chunk). 
- hall_bake HALL_PERCHUNK: partitions meshes into an atlas chunk grid (HALL_ATLAS_CHUNK) by centroid, bakes each chunk's meshes into <out>_cNNN.flm against the full scene. MAXM->8192, setup arrays off-stack (scales to thousands of meshes).
- Verified: 94-mesh zone -> 4 chunks, each non-zero, ~4x per-mesh res of the global atlas.

REMAINING:
1. UNIFORM + SMALLER atlases: per-chunk flms are currently 340-540MB each (4096xH x 9 RGB32F = 108 bytes/texel) and VARY in size. For a texture ARRAY all layers must be one fixed size. Add fixed HALL_ATLAS_W/H (pad), and cap lmres so a chunk fits; consider float16 SH (54 B/texel) to halve file/VRAM. Streaming keeps only visible chunks resident so per-chunk 1024-2048 sq is fine.
2. RENDER (texture array + froxel): load N per-chunk flms into a GL_TEXTURE_2D_ARRAY page table (layer per resident chunk, 9 coeffs). Froxel/cluster buffer (reuse forward+ cluster grid) -> which chunk pages visible -> stream/bind those layers. PBR forward shader: fragment world pos -> chunk id -> page-table layer -> sample uv1 in that layer. One draw of whole scene (meshes spanning chunks OK). Renderer already loads N dmeshes + assigns to chunks by the same grid.
3. Fold per-chunk near/med SDF into each flm (rpg-iudw).

**2026-07-16T00:22:14Z**

RENDER HALF DONE + SEAM FIXED (committed+pushed).
- Texture array: 9 SH coeff atlases as GL_TEXTURE_2D_ARRAY (layer per chunk); per-mesh u_sh_layer; pbr shader sampler2DArray+SHUV; render_forward binds 2D_ARRAY + sets layer per renderable; render_renderable.sh_layer.
- hall_bake HALL_PERCHUNK emits ZLM1 manifest (per-mesh layer+rect, sorted order); hall_lit_dynamic load_sh_arrays builds the array (single 1-layer OR LM_PERCHUNK N-layer via manifest+_cNNN.flm), non-uniform atlas heights padded to array max. Renders ~52-64 fps Iris Xe.
- CHUNK-BOUNDARY SEAM FIXED: per-luxel RNG now seeded by WORLD POSITION (floatBitsToUint hash) not chunk-local index -> noise field identical across chunk borders; 4-chunk now matches whole-scene bake. Verified on 94-mesh column/vault zone at 256spp: blotchy center gone.

REMAINING: (1) scale to the full 400m/1345-mesh zone (froxel streaming so only visible chunk layers resident; current all-resident array is fine for small zones but ~690MB VRAM for 4 layers at 4096x390 -> too big for many layers). (2) float16 SH + fixed/smaller per-chunk atlas to cut file+VRAM. (3) fold per-chunk near/med SDF into flm (rpg-iudw). (4) reduce the 22% no-material voxelize gap (darker indirect bounce off untextured voxels).

**2026-07-16T03:13:48Z**

RENDER HALF COMPLETE (all committed/pushed). Full per-chunk lightmap pipeline works end to end:
- Bake: HALL_PERCHUNK emits per-chunk atlases + ZLM1 manifest (per-mesh layer+rect). geo_scene lets a chunk bake against the whole scene.
- Render: 9 SH coeff atlases as GL_TEXTURE_2D_ARRAY (layer per chunk), per-mesh u_sh_layer; hall_lit_dynamic load_sh_arrays builds the array from manifest+_cNNN.flm.
- Chunk-boundary SEAM fixed (per-luxel RNG seeded by world position, not chunk-local index).
- Per-cell per-light visibility STORE (CS_LIGHTVIS) so the gather samples direct light without per-hit shadow rays; no-material voxels get a fallback albedo.
- Verified: instanced column/vault zones + big-bay diagnostic; color bleed confirmed (green floor -> green columns); ~60-470fps.
REMAINING (optional): the froxel/streaming residency for the FULL 400m zone (all-resident array is fine up to a few chunks; large zones need visible-chunk streaming). Uniform/float16 atlas for VRAM. Fold SDF into flm (rpg-iudw).
