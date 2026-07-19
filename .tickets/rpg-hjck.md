---
id: rpg-hjck
status: open
deps: []
links: []
created: 2026-07-19T07:42:37Z
type: epic
priority: 1
assignee: KMD
tags: [gi, asset, server, client, streaming]
---
# Unify GI/probe/SDF into the real server & client (level pipeline)

Take the GI/probe/SDF stack proven in tests/visual/hall_lit_dynamic.c and wire it into the actual server (tests/examples/demo_server.c) and client (tests/examples/demo_client.c), renamed to server/client, through the existing asset pipeline, job system, render command queue, and scene/GI builders. We have all the features already; this epic is integration + a few greenfield formats/managers, not new rendering tech.

## Design

GROUND TRUTH (from a codebase audit; cite real files):
- Server: tests/examples/demo_server.c (mirror in examples/; Makefile builds the tests/examples copy). Orchestrator = struct demo_ctx; numbered wiring stages in main(); fixed 30Hz tick loop with on_drain/on_physics/on_encode/on_flush stages; physics on its own async thread (phys_tick_runner, post_tick_cb). Starts with an EMPTY world -- geometry only via the editor protocol or --scene edit_level_load. LINKS libheadless.a (NO GL/renderer).
- Client: tests/examples/demo_client.c. main() locals, 2 threads (main + fr_prediction_tick). Triple-buffered body pool + phys_prediction_reconcile. Bodies arrive from the network (BODY_SPAWN/MESH_DATA); renders with an INLINE forward shader; the real renderer + gi_runtime are NOT wired (GI only in the standalone cornell_demo.c). Prediction is integration-only (no colliders streamed). LINKS liball.a (full renderer).
- GI runtime API (include/ferrum/renderer/gi/gi_runtime.h): gi_runtime_init/frame/bind/destroy + set_probe_grid/set_static_volume/set_static_weights/set_sky_ao/set_spec_gain. Inputs: probe positions or a regular grid (index (z*dim[1]+y)*dim[0]+x), SDF chunk prefix (<prefix>_cNNN.sdf), per-frame gi_collider_t boxes, and scene->lights tagged RENDER_LIGHT_FLAG_DYNAMIC_INDIRECT. Coupling contracts: gi cfg.froxel MUST equal fwd cfg.cluster; the only GI<->forward link is render_forward_config.material_extra_bind -> gi_runtime_bind(.., base unit 24). The full 10-step assembly currently lives inline in hall_lit_dynamic.c main() -- must be lifted into a reusable src/ module.
- Asset pipeline: NO central streaming/residency manager. Only async loader is texture-only (src/renderer/resource/resource_loader.c, job_dispatch priority hardcoded 0). job_dispatch(sys,fn,user,int priority,counter) exists but priority is IGNORED in the work-stealing path (src/job/queue.c) -- only honored by the deterministic scheduler. gpu_cmd_queue + GPU_CMD_CUSTOM (render-thread execute hook) is the extension point for mesh/skeleton uploads. Chunk-residency patterns exist: gi_sdf_stream (src/, LRU by visibility, SYNC disk I/O) and sh_stream (only in the demo). Loaders exist for everything: gltf/glb (gltf_scene_load + gltf_scene_create_static/skeletal_mesh), fvma (static/skeletal_mesh_create_from_fvma), fskel (fskel_load/write), dmesh (dmesh_load), obj (obj_mesh_load), lightmap .flm (lm_lightmap_load), sdf .sdf (lm_sdf_load). NO scene-descriptor format and NO on-disk probe format exist (both greenfield).

TARGET (user spec):
- SERVER loads the level's collision geo, a very low-res SDF for CPU-side graph building, and a coarse set of priority/fixed light probes; from those it computes direct lighting + baked indirect specifically for visibility/stealth queries; then runs the physics tick. All of this must be HEADLESS (libheadless, no GL) -- reuse the CPU lm_* lightmap code + SDF sampling, NOT the GPU compute probe path.
- CLIENT loads a scene from a standard scene file (glTF) with skeletons packed to fskel and meshes loadable as external fvma/glb/dmesh/obj. Everything flows through the existing asset pipeline, which streams assets and level chunks from disk on the job system by SERVER-ASSIGNED priority. Streamed assets: lightmaps, manually-placed probes, probe-grid importance volumes, sdf/voxel chunks, skeletons (for skinning), and physics colliders (for client-side prediction -- see demo_client; networked physics already exists). Client injects posed/animated assets into the chunked SDF of the dynamic geo so the probe builder (and later dynamic audio propagation) can use them.
- Rename demo_server->server, demo_client->client; keep the orchestrator expandable (register subsystems/stages). Reuse the existing job system, render command queue, and scene/GI builders wherever possible.

APPROACH: build the greenfield formats first (scene descriptor, on-disk probes), then the reusable render-world builder + generic prioritized streamer, then wire client and server, then the cross-cutting pieces (collider streaming, dynamic-SDF injection, build/lib split). See child tasks + dep graph.


## Notes

**2026-07-19T07:48:29Z**

Correction (design): streaming priority is the ASSET STREAMER's admission/eviction policy, not job-scheduler priority. Server suggests a priority order; client admits top-priority assets within RAM + VRAM residency budgets, evicting lowest-priority under pressure, so needed assets are resident before use (no cache-miss stall). Bounded in-flight loads refilled from the queue head make effective load order == priority order regardless of job scheduling. Closed rpg-n3cc (honor job priority) as unneeded; folded the real model into rpg-nbp2.

**2026-07-19T07:56:08Z**

Clarifications: (1) Spatial CHUNKING applies specifically to the baker-generated LIGHT DATA -- lightmap SH chunks (<lm>_cNNN.flm + _manifest.bin), SDF/albedo-voxel chunks (<lm>_cNNN.sdf, RGBA32F rgb=albedo/a=distance, per-chunk world box, sparse), and their folded-in far-field (lm_farfield). Already generated by the baker and paged in hall_lit_dynamic.c (sh_stream + gi_sdf_stream). This is NOT the whole streaming system -- meshes/skeletons/colliders are one-shot streamable assets (rpg-nbp2). (2) Probes stay auto-generated from a resolution by default; the pipeline only adds optional manual probes, resolution-by-distance/LOD, and AABB box importance overrides (rpg-ft0g). Which probes load/generate is GATED by which light-data chunks are resident (probe belongs to the chunk whose box contains it), under the same server-suggested priority.
