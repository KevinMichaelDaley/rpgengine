---
id: rpg-ojuq
status: open
deps: []
links: []
created: 2026-07-16T03:20:09Z
type: task
priority: 1
assignee: KMD
parent: rpg-yfa4
---
# Froxel lightmap-index prepass + streaming page-table residency


## Notes

**2026-07-16T03:20:27Z**

GOAL: stream only VISIBLE chunk lightmap pages into a bounded texture array so the full 400m zone renders at fixed VRAM (the all-resident array from rpg-yfa4 only scales to a few chunks).

DESIGN:
1. CHUNK ID vs LAYER: the manifest's per-mesh 'layer' becomes the stable CHUNK ID (0..n_chunks). The GL_TEXTURE_2D_ARRAY has MAX_RESIDENT layers. A PAGE TABLE maps chunk_id -> resident array layer (-1 = not resident).
2. LIGHTMAP-INDEX PREPASS: a cheap froxel/tiled pass over the scene; the fragment marks its mesh's chunk visible in an SSBO (uint chunk_visible[n_chunks], atomicOr / write 1). This yields the exact set of chunks with on-screen geometry (froxel = the tiled visibility grid; per-fragment marking is the simplest correct form).
3. RESIDENCY (CPU, per frame or on change): read chunk_visible[]; for each newly-visible chunk not resident, assign a free/LRU-evicted layer and glTexSubImage3D-upload its coeffs from RAM-cached flm data; update the page table. Bounded to MAX_RESIDENT.
4. MAIN PASS: pbr shader samples sh_array at layer = pageTable[u_sh_layer]; non-resident (-1) -> ambient fallback. Pass the page table as a small uniform int[]/SSBO.

Keep all N chunk coeff buffers in host RAM (from the per-chunk flms) for streaming uploads. Reuse the forward+ cluster grid concept for the froxel tiling. Files: tests/visual/hall_lit_dynamic.c (prepass + residency + array), src/renderer/pbr_shader.c (page-table lookup), maybe a small prepass shader.

**2026-07-16T03:32:44Z**

DONE (committed/pushed). LM_STREAM streams only on-screen chunks into a bounded array:
- sh_stream_load caches all per-chunk coeffs in RAM + creates a SH_MAX_RESIDENT(12)-layer array.
- Lightmap-index prepass: low-res R32UI target, fragment writes chunk id; readback -> visible set (GL 3.3 -> colour target not SSBO).
- Residency: page visible chunks via glTexSubImage3D, LRU evict; page table chunk->layer(-1); shader gates SH on layer>=0; CPU sets per-mesh layer per frame.
- Verified on 94-mesh/4-chunk zone (identical to all-resident).
FOLLOW-UPS: async PBO readback (synchronous glReadPixels stalls frame 0); run prepass every N frames (residency near-static); per-mesh model in prepass (currently identity). Then run the FULL 400m zone to show eviction at scale.
