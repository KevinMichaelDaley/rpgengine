---
id: rpg-iudw
status: open
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
