---
id: rpg-tjtp
status: open
deps: []
links: []
created: 2026-07-15T08:30:01Z
type: epic
priority: 1
assignee: KMD
---
# GPU lightmap baker: port the GI gather/bounce pass to a compute shader


## Notes

**2026-07-15T08:30:36Z**

## Motivation

Production lightmap bakes are painfully slow. The bottleneck is the path-traced GI gather (src/lightmap/lm_gi_gather.c: lm_gi_gather), a per-luxel stratified hemisphere of primary rays path-traced through the voxelized SVO. It runs CPU-multithreaded (lm_parallel), but locally hall_bake is restricted to 1 core (2+ threads crash this box), and full 1024-spp/3-bounce bakes over a 4096 atlas take a very long time. Chimera is a dead end (glibc IFUNC libm FP non-portability -> divergent bakes). The fix is to move the gather/bounce hot loop to a GPU compute shader.

## What to port (and what NOT to)

Port ONLY the GI gather/bounce inner loop. Leave on the CPU: atlas packing (lm_atlas), direct/emissive luxel seeding (lm_direct, lm_mesh_luxel), the SH atlas assembly + dilation + .flm serialization (lm_lightmap_file), and the SVO voxelization + mip build (lm_svo_voxelize, lm_svo_mip) which happen once before the gather.

## Current gather, concretely (the thing to reimplement in GLSL compute)

- Scene = npc_svo_grid_t (include/ferrum/npc/npc_svo.h). Already GPU-friendly: a FLAT array `nodes[]` of 64-byte npc_svo_node_t, each with children[8] (child indices or NPC_SVO_INVALID_NODE), occupancy/flags/material, and a per-node prefiltered pyramid diffuse[3]/emissive[3] (the "mip" a distant cone samples). Plus voxel_size (m per smallest voxel) and world bounds.
- Visibility = DDA march through the SVO (src/lightmap/lm_visibility.c: lm_dda / lm_visibility_trace), stepping by the smallest voxel size, tmin self-hit guard = svo->voxel_size.
- Per primary ray (lm_gi_trace): NEAR field (hit dist <= transition) reads the hit voxel's diffuse/emissive material exactly, adds emission + direct sun via a shadow ray (lm_gi_direct, bias = 1.5*voxel_size), scatters a cosine-weighted bounce, and continues (iterative path trace up to `bounces`). Once ray length > transition it BECOMES A CONE: sample a coarse ancestor mip level (lm_gi_cone_levels from cone half-angle vs voxel_size) and terminate. Escape -> sky (lm_sky).
- Accumulation: incident radiance -> SH9 (lm_sh), summed into a caller-provided accum (3 SH coeff-sets per luxel), NOT the luxel's own SH, so batches of ~64 samples with different seeds form a running mean (accum/batch_count). Signature: lm_gi_gather(lm, accum, svo, lights, n_lights, sky, vnormal, transition, maxdist, samples, bounces, seed, n_threads).

## GPU design (this epic)

1. SVO -> GPU packing (rpg-8je5): repack nodes[] + per-node diffuse/emissive into SSBOs (node struct is already flat/index-based; drop parent/nav fields not needed by the gather), plus a luxel buffer (world pos + normal per luxel, from lm_lightmap), lights buffer, sky params, and a uniform block (voxel_size, bounds, transition, maxdist, bounces).
2. Compute kernel (rpg-8sv9): one thread per luxel (or per luxel x sample tile), reimplement DDA + near path-trace w/ cosine bounces + shadow-ray sun + far cone/mip + sky escape, in-shader PCG/xorshift RNG seeded by (luxel_index, sample, batch). Output per-luxel SH9 partial sums.
3. Progressive batching (rpg-yzmp): dispatch N batches, accumulate SH9 into an SSBO running sum, read back per batch for a live preview or once at the end; mirror the current on_batch running-mean semantics.
4. Offline compute context + integration (rpg-k4lk): stand up a headless GL 4.3+ / compute context for the baker (it currently runs headless/no-GL), wire the GPU gather into lm_mesh_bake behind a flag with the CPU path as reference fallback, and validate PARITY vs CPU within Monte-Carlo tolerance (not bit-exact; GPU/CPU FP differ -- compare converged means + visual on the hall). The renderer's resource paradigm (rpg-h553: gpu_cmd_queue/executor) can create the SSBOs, but the baker is offline so a dedicated context is fine.

## Acceptance

The hall production bake (1024 spp / 3 bounces / 4096 atlas) that currently takes many minutes on 1 CPU core completes in a small fraction of the time on the GPU, producing a lightmap that matches the CPU bake within Monte-Carlo tolerance (verified numerically + by rendering hall_lit_dynamic). CPU gather retained as reference. Clean under -Wpedantic; TDD for the packing + host-side batch/readback logic (the GLSL kernel validated by parity).
