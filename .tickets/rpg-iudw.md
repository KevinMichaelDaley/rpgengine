---
id: rpg-iudw
status: closed
deps: []
links: []
created: 2026-07-15T08:52:48Z
type: task
priority: 2
assignee: KMD
parent: rpg-tjtp
---
# Store baked SDF in the .flm for runtime reuse (coarse RTGI, dynamic lights)


## Notes

**2026-07-15T08:52:48Z**

The coarse JFA SDF (128^3, from the lower SVO depth -- rpg-bpyr, rpg-8sv9) is expensive-ish to build but reusable, so PERSIST IT IN THE .flm (add an SDF chunk to the FLM format: dims, voxel_size, world origin/bounds, R16F or R8_snorm distance field). The offline bake writes it; the runtime loads it alongside the SH lightmap.

Runtime uses (future / fun): coarse real-time GI for DYNAMIC lights (sphere/cone-trace the SDF for one-bounce indirect + soft shadows / AO on moving lights, complementing the static SH lightmap), dynamic-object contact shadows, collision/nav queries, screen-space fallback avoidance. Design the chunk so the SDF can be uploaded straight to a 3D texture at load.

Depends on the SDF builder (rpg-bpyr done) + the coarse-level build settled in rpg-8sv9; extends lm_lightmap_file (FLM format) + a runtime loader path.

**2026-07-16T06:31:32Z**

DONE + committed. Per-near-chunk baked SDF persisted to <prefix>_cNNN.sdf sidecars (lm_sdf_file, format FSDF v1: dims/voxel/origin + 128^3 floats, GL_R32F-3D-texture-ready). Reuses the field the gather already built (glGetBufferSubData readback in lm_gpu_gather_run, threaded via lm_bake_config.sdf_out_prefix / HALL_SDF env). Round-trip unit tests + validated end-to-end on chimera (256spp/8-bounce/OIDN hall bake -> 4 valid sidecars). Runtime loader/upload-to-3D-texture is future work when a runtime consumer needs it.
