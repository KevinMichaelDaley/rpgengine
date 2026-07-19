---
id: rpg-nbp2
status: open
deps: [rpg-51nf]
links: []
created: 2026-07-19T07:44:03Z
type: task
priority: 1
assignee: KMD
parent: rpg-hjck
tags: [asset, streaming, job]
---
# Generic prioritized asset-streaming manager

Extend the texture-only resource_loader into a general async streamer for meshes/skeletons/lightmaps/sdf+voxel/probe chunks. The streamer OWNS the priority: it keeps a priority-ordered request queue (priorities suggested by the server) and admits assets within a RAM residency budget (disk->RAM) and a VRAM residency budget (RAM->VRAM), always working from the top of the queue and evicting the lowest-priority resident under pressure. The job system is only the async executor for the individual load/upload jobs; GPU uploads go through gpu_cmd_queue GPU_CMD_CUSTOM. Unify the two ad-hoc chunk-residency managers (gi_sdf_stream in src/, sh_stream in the demo) under this one model.

## Design

PRIORITY LIVES HERE, NOT IN THE JOB SCHEDULER (see closed rpg-n3cc). The server sends a suggested priority order over the level's asset/chunk ids (rpg-3ldk); the client streams "as many as fit in RAM, copies as many as fit in VRAM, starting with the suggested ones" so the assets the player needs are resident before they're referenced (no stall / cache miss).

Model:
- A priority-ordered request queue (max-heap / bucketed) keyed by asset id, priority set/updated by the server hints and by local interest.
- Two-tier residency with explicit budgets: RAM_resident (disk->RAM decode) and VRAM_resident (RAM->VRAM upload), each an LRU-within-priority set. Under budget pressure, evict the lowest-priority (then LRU) resident, not the newest.
- Bounded in-flight loads (N worker slots): pop the highest-priority not-yet-resident asset, dispatch a load fiber, refill from the queue head as each completes. Because refills always come from the top, effective completion order == priority order regardless of the work-stealing pool's internal scheduling -- so job_dispatch priority is irrelevant (plain job_dispatch(...,0,counter) is fine).

Reuse: resource_loader_t pattern (handle reserved immediately via gpu_registry_alloc so a not-yet-resident asset has a valid handle; fiber does disk IO + decode; GPU_CMD_CUSTOM runs the real create fn on the render thread). Wrap each existing loader (gltf/fvma/fskel/dmesh/obj/flm/sdf/probe) behind the request API keyed by asset id + priority. The headless server uses the same manager minus the VRAM tier (RAM residency only; no GPU upload step).

TWO STREAMABLE CLASSES -- do not conflate them:
(a) ONE-SHOT assets: meshes (fvma/glb/dmesh/obj), skeletons (fskel), textures, colliders. Requested by id, loaded once, resident until unloaded. These are the bulk of the streamer.
(b) SPATIALLY-CHUNKED BAKED LIGHT DATA: this is the ONLY thing that is spatially chunk-paged, and it already exists + is supported in hall_lit_dynamic.c. The baker emits, per spatial chunk (sparse -- empty regions produce no chunk; see great_hall.flm_c000..c008.sdf, c005 absent):
   - lightmap SH chunks: <lm>_cNNN.flm (+ <lm>_manifest.bin per-mesh layer/atlas-rect) -- paged today by the demo's sh_stream into a GL_TEXTURE_2D_ARRAY.
   - SDF / ALBEDO-VOXEL chunks: <lm>_cNNN.sdf, RGBA32F (rgb = static albedo voxels, a = signed distance), each carrying a world-space box (gi_sdf_chunk.origin/dims) -- paged today by gi_sdf_stream (src/, LRU by vis-prepass) into RGBA32F 3D textures.
   - the far-field contribution the baker computes (lm_farfield.c SVO distant-reflector gather + sky, lm_scene farfield_*): folded into the baked chunk data so a resident chunk is self-contained for both near light and distant reflectors.
   Promote sh_stream out of the demo into src/ next to gi_sdf_stream and give BOTH the common two-tier residency interface, keyed on visibility/interest -> priority (they become clients of this streamer). Chunk id <-> world box is the spatial key.

CHUNK RESIDENCY DRIVES PROBES: which probes are loaded/generated follows which light-data chunks are resident (rpg-ft0g) -- a probe belongs to the chunk whose box contains it; page a chunk in -> its probes appear, evict it -> they drop, under the same server-suggested priority order.

## Acceptance Criteria

Streamer loads a mesh, a skeleton, a lightmap page, and an SDF chunk asynchronously, admitting in priority order within configurable RAM and VRAM budgets; raising an asset's priority pulls it resident ahead of lower-priority ones; under budget pressure the lowest-priority resident is evicted first; gi_sdf_stream + lightmap chunks share the residency model; the headless (no-GL) build streams the CPU-side assets (RAM tier only); an integration test streams a small scene by priority without blocking the main thread and shows the top-priority assets resident first.

